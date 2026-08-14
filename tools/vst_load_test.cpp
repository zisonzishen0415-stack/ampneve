// Minimal VST3 loader: instantiates a plugin (processor + controller +
// editor view) exactly like a DAW would, to reproduce load crashes locally.
#include <cstdio>
#include <cstring>
#include <windows.h>
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "pluginterfaces/gui/iplugview.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

// The JUCE-bundled VST3 SDK omits fuid.cpp; provide the tiny members here.
static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
namespace Steinberg {
FUID::FUID() { for (int i = 0; i < 16; ++i) data[i] = 0; }
bool FUID::fromString(const char* s) {
    if (!s) return false;
    for (int i = 0; i < 16; ++i) {
        int hi = hexVal(s[2 * i]), lo = hexVal(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        data[i] = (char)((hi << 4) | lo);
    }
    return true;
}
void FUID::toString(char* s) const {
    for (int i = 0; i < 16; ++i) sprintf(s + 2 * i, "%02X", (unsigned char)data[i]);
    s[32] = 0;
}
bool FUID::isValid() const {
    for (int i = 0; i < 16; ++i) if (data[i]) return true;
    return false;
}
}

// VST3 3.7.8 ABI in JUCE's build: createInstance args are 16 RAW TUID bytes
// (the FIDString params are memcpy'd as sizeof(TUID)). The iid bytes are the
// INLINE_UID order = each 4-byte group of the DECLARE_CLASS_IID constants
// byte-reversed.
// Interface FUIDs in TUID memory order on Windows (VST3 SDK COM_COMPATIBLE
// layout): l1 little-endian, l2 stored as high-word-then-low-word each
// little-endian, l3/l4 big-endian. Verified against this JUCE 7.0.12 build
// (brute-force probe, work/iid_probe.cpp).
static const char* kIidAudioProcessorStr = "993F0442DAB73C45A569E79D9AAEC33D";
static const char* kIidEditControllerStr = "E3BBD7DC42778D44A874AACC979C759E";
static const char* kIidComponentStr      = "31FF31E8D5F20143928EBBEE25697802";
static const char* kIidFUnknownStr       = "0000000000000000C000000000000046";
static const char* kIidHostAppStr        = "CC95E5582DDB69498B6AAF8C36A664E5";

static void hexToBytes(const char* h, char* out) {
    for (int k = 0; k < 16; ++k)
        out[k] = (char)((hexVal(h[2 * k]) << 4) | hexVal(h[2 * k + 1]));
}

static bool tuidEqual(const TUID a, const char* hex) {
    char b[16];
    hexToBytes(hex, b);
    return memcmp(a, b, 16) == 0;
}

static FUnknown* tryInstance(IPluginFactory* f, const TUID& tuid, const char* iidHexStr) {
    // raw-bytes ABI: both args are 16-byte TUID pointers (FIDString is
    // memcpy'd as sizeof(TUID) by JUCE's implementation)
    char iidBytes[16];
    for (int k = 0; k < 16; ++k) {
        int hi = hexVal(iidHexStr[2 * k]), lo = hexVal(iidHexStr[2 * k + 1]);
        iidBytes[k] = (char)((hi << 4) | lo);
    }
    FUnknown* out = nullptr;
    tresult r = f->createInstance((const char*)tuid, (const char*)iidBytes, (void**)&out);
    printf("  createInstance raw iid=%s -> r=%d %s\n", iidHexStr, (int)r, out ? "OK" : "null");
    return out;
}

// Minimal host so IComponent::initialize() gets a valid context (nullptr is
// rejected by JUCE's wrapper).
class TestHost : public IHostApplication
{
public:
    TestHost() : refs (1) {}
    tresult PLUGIN_API getName (String128 name) override
    {
        const char* n = "vst_load_test";
        int i = 0;
        for (; n[i] != 0 && i < 127; ++i) name[i] = n[i];
        name[i] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance (TUID, TUID, void** obj) override
    {
        *obj = nullptr;
        return kNotImplemented;
    }
    tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
    {
        *obj = nullptr;
        if (tuidEqual (iid, kIidHostAppStr) || tuidEqual (iid, kIidFUnknownStr))
        {
            addRef();
            *obj = this;
            return kResultOk;
        }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }

private:
    uint32 refs;
};

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: vst_load_test <plugin.dll>\n"); return 2; }
    HMODULE mod = LoadLibraryA(argv[1]);
    if (!mod) { printf("FAIL: LoadLibrary err=%lu\n", GetLastError()); return 2; }
    auto getFactory = (IPluginFactory * (*)()) GetProcAddress(mod, "GetPluginFactory");
    if (!getFactory) { printf("FAIL: GetPluginFactory missing\n"); return 2; }
    IPluginFactory* f = getFactory();
    if (!f) { printf("FAIL: factory null\n"); return 2; }

    printf("classes: %d\n", f->countClasses());
    TUID audioTuid = {0}, ctrlTuid = {0};
    for (int i = 0; i < f->countClasses(); ++i) {
        PClassInfo info;
        if (f->getClassInfo(i, &info) == kResultOk) {
            char buf[40];
            for (int k = 0; k < 16; ++k) sprintf(buf + 2 * k, "%02x", (unsigned char)info.cid[k]);
            buf[32] = 0;
            printf("  [%d] %-40s %s\n", i, buf, info.category);
            if (i == 0) memcpy(audioTuid, info.cid, 16);  // JUCE order: audio, controller, compat
            if (i == 1) memcpy(ctrlTuid, info.cid, 16);
        }
    }

    // getClassInfo returns PClassInfo (info2) bytes and createInstance
    // matches against infoW.cid which fromAscii() memcpy's from info2 -
    // same byte order, use the class TUID straight from getClassInfo.
    // Interface IIDs are the canonical DECLARE_CLASS_IID byte order (the
    // plugin's FUID::toTUID() memcpy's its data as-is). Passing the class
    // UID as the iid is the classic bug here -> E_NOINTERFACE.
    FUnknown* ap = tryInstance(f, audioTuid, kIidAudioProcessorStr);
    if (!ap) return 1;
    auto* proc = (IAudioProcessor*) ap;
    IComponent* comp = nullptr;
    char compIid[16];
    hexToBytes (kIidComponentStr, compIid);
    ap->queryInterface (compIid, (void**) &comp);
    if (comp == nullptr) { printf("FAIL: processor does not expose IComponent\n"); return 1; }

    // controller instance: IEditController extends IComponent
    FUnknown* cp = tryInstance(f, ctrlTuid, kIidEditControllerStr);
    if (!cp) return 1;
    auto* ctrl = (IEditController*) cp;

    TestHost host;
    if (comp->initialize(&host) != kResultOk) { printf("FAIL: component initialize\n"); return 1; }
    comp->setActive(true);
    printf("created processor + controller\n");

    // editor view - the REAPER crash path (created via the edit controller)
    IPlugView* view = nullptr;
    if (ctrl->createView("editor") != nullptr && (view = ctrl->createView("editor")) != nullptr) {
        printf("created editor view\n");
        view->setFrame(nullptr);
        view->attached(nullptr, nullptr);
        view->removed();
        view->release();
        printf("editor lifecycle ok\n");
    } else {
        printf("WARN: no editor view (createView failed)\n");
    }

    comp->setActive(false);
    comp->terminate();
    ap->release();
    cp->release();
    f->release();
    FreeLibrary(mod);
    printf("OK: full lifecycle completed\n");
    return 0;
}
