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
static const char* kIidAudioProcessorStr = "993F04423C45DAB79DE769A53DC3AE9A";
static const char* kIidEditControllerStr = "E3BBD7DC8D444277CCAA74A89E759C97";

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

    // processor instance: also implements IComponent (VST3 contract)
    // cid variants: getClassInfo byte order (info2) vs moduleinfo-JSON order
    char cidJson[16];
    for (int k = 0; k < 16; ++k) {
        int hi = hexVal("ABCDEF019182FAEB416D4E76416D4E76"[2 * k]);
        int lo = hexVal("ABCDEF019182FAEB416D4E76416D4E76"[2 * k + 1]);
        cidJson[k] = (char)((hi << 4) | lo);
    }
    FUnknown* ap = tryInstance(f, audioTuid, "01EFCDAB8291FAEB416D4E76416D4E76");
    if (!ap) ap = tryInstance(f, cidJson, "01EFCDAB8291FAEB416D4E76416D4E76");
    if (!ap) return 1;
    auto* proc = (IAudioProcessor*)ap;
    auto* comp = (IComponent*)ap;

    // controller instance: IEditController extends IComponent
    char ctrlJson[16];
    for (int k = 0; k < 16; ++k) {
        int hi = hexVal("ABCDEF011234ABCD416D4E76416D4E76"[2 * k]);
        int lo = hexVal("ABCDEF011234ABCD416D4E76416D4E76"[2 * k + 1]);
        ctrlJson[k] = (char)((hi << 4) | lo);
    }
    FUnknown* cp = tryInstance(f, ctrlTuid, "01EFCDAB3412CDAB416D4E76416D4E76");
    if (!cp) cp = tryInstance(f, ctrlJson, "01EFCDAB3412CDAB416D4E76416D4E76");
    if (!cp) return 1;
    auto* ctrl = (IEditController*)cp;
    auto* ctrlComp = (IComponent*)cp;

    if (comp->initialize(nullptr) != kResultOk) { printf("FAIL: component initialize\n"); return 1; }
    if (ctrlComp->initialize(nullptr) != kResultOk) { printf("FAIL: controller initialize\n"); return 1; }
    comp->setActive(true);
    ctrlComp->setActive(true);
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
    ctrlComp->setActive(false);
    comp->terminate();
    ctrlComp->terminate();
    ap->release();
    cp->release();
    f->release();
    FreeLibrary(mod);
    printf("OK: full lifecycle completed\n");
    return 0;
}
