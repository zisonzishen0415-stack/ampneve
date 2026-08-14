#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

/* Pedal-style rotary knob: dark body with a pointer line, like the G1on /
   MS-series parameter knobs. */
class PedalKnobLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(5.0f);
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        auto r = bounds.getWidth() * 0.5f;
        g.setColour(juce::Colour(0xff272a30));
        g.fillEllipse(bounds);
        g.setColour(juce::Colour(0xff5b6069));
        g.drawEllipse(bounds, 2.0f);
        float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        juce::Point<float> tip(cx + r * 0.72f * std::cos(angle),
                               cy + r * 0.72f * std::sin(angle));
        g.setColour(juce::Colour(0xffe8eef5));
        g.drawLine(cx, cy, tip.x, tip.y, 3.0f);
        g.fillEllipse(cx - 3.5f, cy - 3.5f, 7.0f, 7.0f);
    }
};

const char* AmpNeveAudioProcessorEditor::ids[3][3] = {
    {"bass", "mid", "treble"},
    {"gain", "master", "level"},
    {"neve", "cab", "presence"}
};
const char* AmpNeveAudioProcessorEditor::names[3][3] = {
    {"Bass", "Mid", "Treble"},
    {"Gain", "Master", "Level"},
    {"Neve", "Cab", "Presence"}
};

/* Factory presets: {gain, bass, mid, treble, master, level, neve, cab,
 * presence, input, voice}. The first one mirrors the plugin defaults. */
const AmpNeveAudioProcessorEditor::FactoryPreset
AmpNeveAudioProcessorEditor::factoryPresets[5] = {
    { "Nashville Clean", { 0.20f, 0.55f, 0.45f, 0.60f, 0.45f, 0.75f, 1.00f, 0.45f, 0.70f, 1.00f, 0.0f } },
    { "Edge / Breakup",  { 0.35f, 0.50f, 0.50f, 0.50f, 0.55f, 0.75f, 1.00f, 0.50f, 0.85f, 1.00f, 0.0f } },
    { "British Crunch",  { 0.55f, 0.50f, 0.40f, 0.60f, 0.65f, 0.70f, 1.00f, 0.55f, 0.80f, 1.00f, 0.0f } },
    { "High Gain",       { 0.85f, 0.50f, 0.45f, 0.55f, 0.90f, 0.70f, 1.00f, 0.55f, 0.80f, 1.00f, 0.0f } },
    { "Emo / Edge",      { 0.40f, 0.50f, 0.60f, 0.55f, 0.60f, 0.75f, 1.00f, 0.50f, 0.80f, 1.00f, 1.0f } }
};

static const juce::Colour lcdBg(0xff0d2239);     /* deep blue LCD backlight */
static const juce::Colour lcdDim(0xff5a7ea0);    /* dimmed pixel */
static const juce::Colour lcdLit(0xffcfe9ff);    /* lit pixel */
static const juce::Colour body(0xff20232a);      /* pedal chassis */
static const juce::Colour bezel(0xff4a4e57);     /* screen bezel */
static const juce::Colour mGreen(0xff4ade80);    /* input level ok */
static const juce::Colour mYellow(0xffe8c34a);   /* input level warm */
static const juce::Colour mRed(0xffe05252);      /* input level hot */
static const juce::Colour mBg(0xff14304c);       /* meter trough */

AmpNeveAudioProcessorEditor::AmpNeveAudioProcessorEditor(AmpNeveAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    pageButton.setButtonText("PAGE");
    pageButton.onClick = [this] { setPage((currentPage + 1) % 3); };
    addAndMakeVisible(pageButton);

    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "bypass", bypassButton);

    voiceButton.setButtonText("VOICE");
    voiceButton.setClickingTogglesState(true);
    addAndMakeVisible(voiceButton);
    voiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "voice", voiceButton);

    /* preset row: factory + user presets (XML files in the app-data dir) */
    presetBox.setTextWhenNothingSelected("PRESET");
    presetBox.onChange = [this] {
        if (loadingPresets) return;
        int sel = presetBox.getSelectedId();
        if (sel >= 1 && sel <= 5) applyFactoryPreset(sel - 1);
        else if (sel >= 100 && sel - 100 < userPresetFiles.size())
            loadUserPreset(userPresetFiles[sel - 100]);
    };
    saveButton.onClick = [this] { saveUserPreset(); };
    delButton.onClick  = [this] { deleteUserPreset(); };
    addAndMakeVisible(presetBox);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(delButton);

    for (int i = 0; i < 3; ++i) {
        knobs[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knobs[i].setRange(0.0, 1.0, 0.001);
        /* classic pedal gauge: min lower-left, mid straight up, max lower-right */
        knobs[i].setRotaryParameters(juce::MathConstants<float>::pi * 0.75f,
                                     juce::MathConstants<float>::pi * 2.25f, true);
        knobs[i].setScrollWheelEnabled(true);
        /* drag-start focus: only when the user is actually dragging */
        knobs[i].onValueChange = [this, i] {
            if (knobs[i].isMouseButtonDown()) setFocus(i);
        };
        addAndMakeVisible(knobs[i]);
    }
    /* dedicated Input knob (hardware INPUT VOL), outside the page knobs */
    inputKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    inputKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    inputKnob.setRange(0.0, 1.0, 0.001);
    inputKnob.setRotaryParameters(juce::MathConstants<float>::pi * 0.75f,
                                  juce::MathConstants<float>::pi * 2.25f, true);
    inputKnob.setScrollWheelEnabled(true);
    inputKnob.setDoubleClickReturnValue(true, 1.0);   /* INPUT reset */
    addAndMakeVisible(inputKnob);
    inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "input", inputKnob);

    knobLaf = std::make_unique<PedalKnobLookAndFeel>();
    setLookAndFeel(knobLaf.get());
    /* keep the pedal chassis aspect ratio when the host resizes the editor */
    resizeConstrainer = std::make_unique<juce::ComponentBoundsConstrainer>();
    resizeConstrainer->setFixedAspectRatio(400.0 / 420.0);
    setConstrainer(resizeConstrainer.get());
    setResizeLimits(340, 357, 640, 672);
    setResizable(true, false);
    setPage(0);
    refreshPresetList();
    setSize(400, 420);
    startTimerHz(30);   /* keep LCD values + input meter live */
}

AmpNeveAudioProcessorEditor::~AmpNeveAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

juce::Rectangle<int> AmpNeveAudioProcessorEditor::lcdRect() const {
    return { 16, 16, getWidth() - 32, 186 };
}

juce::Rectangle<int> AmpNeveAudioProcessorEditor::slotRect(int slot) const {
    auto screen = lcdRect().reduced(8, 8);
    auto bottom = screen.removeFromBottom(56);
    int w = bottom.getWidth() / 3;
    juce::Rectangle<int> r(bottom.getX() + slot * w, bottom.getY(), w, bottom.getHeight());
    return r.reduced(4, 2);
}

juce::Rectangle<int> AmpNeveAudioProcessorEditor::knobRect(int index) const {
    auto lcd = lcdRect();
    /* knob column spans from below the LCD to above the preset/button rows;
     * the returned rect is always square so the rotary is a circle at any
     * editor size (a stretched knob is the "aspect distortion" bug). */
    int top = lcd.getBottom() + 12;
    int bottom = getHeight() - 34 - 30 - 8;
    int h = bottom - top;
    if (h < 40) h = 40;
    auto kArea = juce::Rectangle<int>(lcd.getX(), top, lcd.getWidth(), h);
    int w = kArea.getWidth() / 4;
    int side = (w < h) ? w : h;
    int x = kArea.getX() + index * w + (w - side) / 2;
    int y = kArea.getY() + (h - side) / 2;
    return juce::Rectangle<int>(x, y, side, side);
}

void AmpNeveAudioProcessorEditor::setPage(int page) {
    currentPage = page;
    for (int i = 0; i < 3; ++i) {
        attachments[i].reset();
        bool has = (ids[currentPage][i][0] != '\0');
        knobs[i].setEnabled(has);
        if (has) {
            attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, ids[currentPage][i], knobs[i]);
            /* double-click returns the knob to the plugin's default */
            if (auto* p = processor.apvts.getParameter(ids[currentPage][i]))
                knobs[i].setDoubleClickReturnValue(true, (double)p->getDefaultValue());
        }
    }
    repaint();
}

void AmpNeveAudioProcessorEditor::setFocus(int slot) {
    focusedSlot = slot;
    repaint();
}

void AmpNeveAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 3; ++i) {
        if (slotRect(i).contains(e.getPosition())) {
            setFocus(i);
            return;
        }
    }
}

void AmpNeveAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(body);

    auto lcd = lcdRect();
    g.setColour(bezel);
    g.fillRect(lcd);
    auto screen = lcd.reduced(5);
    g.setColour(lcdBg);
    g.fillRect(screen);

    /* header: effect name + page */
    auto header = screen.removeFromTop(30);
    g.setColour(lcdLit);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
    g.drawText("AMPNEVE", header.removeFromLeft(104), juce::Justification::centredLeft, false);
    /* build label: proves which IR generation is loaded (v16 = 2026-08-14) */
    g.setColour(lcdDim);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    g.drawText("v16 IR", header.removeFromLeft(46), juce::Justification::centredLeft, false);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
    auto pageArea = header.removeFromRight(42);
    g.drawText(juce::String("P") + juce::String(currentPage + 1), pageArea,
               juce::Justification::centredRight, false);
    auto* pVoice = processor.apvts.getRawParameterValue("voice");
    bool emo = pVoice != nullptr && pVoice->load() > 0.5f;
    g.drawText(emo ? "EMO" : "NASHVILLE", header, juce::Justification::centredRight, false);

    /* input level meter: label + horizontal bar (raw input peak) + dB text */
    {
        auto meter = screen.removeFromTop(58);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        auto label = meter.removeFromLeft(34);
        g.setColour(lcdDim);
        g.drawText("IN", label, juce::Justification::centredLeft, false);

        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        auto dbText = meter.removeFromRight(64);
        auto bar = meter.removeFromTop(14);

        float level = processor.inMeter.load(std::memory_order_relaxed);
        float peak  = processor.inMeterPeak.load(std::memory_order_relaxed);
        auto toDb = [](float v) {
            if (v < 1e-5f) return -60.0f;
            float db = 20.0f * std::log10f(v);
            return db < -60.0f ? -60.0f : db;
        };
        float db = toDb(level);
        float pkDb = toDb(peak);

        g.setColour(mBg);
        g.fillRect(bar);
        float frac = (db + 60.0f) / 60.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        juce::Colour col = db < -12.0f ? mGreen : (db < -6.0f ? mYellow : mRed);
        g.setColour(col);
        if (frac > 0.0f)
            g.fillRect(bar.withWidth((int)(bar.getWidth() * frac)));

        /* peak-hold marker */
        float pkFrac = (pkDb + 60.0f) / 60.0f;
        if (pkFrac < 0.0f) pkFrac = 0.0f;
        if (pkFrac > 1.0f) pkFrac = 1.0f;
        g.setColour(lcdLit);
        int px = bar.getX() + (int)(bar.getWidth() * pkFrac);
        g.fillRect(px, bar.getY(), 2, bar.getHeight());

        g.setColour(lcdDim);
        g.drawText(juce::String::formatted("%.1f dB", db), dbText,
                   juce::Justification::centredRight, false);
        auto hint = meter;
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        g.setColour(lcdDim);
        g.drawText("ref -12..-6 dBFS DI", hint,
                   juce::Justification::centredLeft, false);
    }

    /* parameter slots: value bar + label */
    for (int i = 0; i < 3; ++i) {
        auto slot = slotRect(i);
        g.setColour(lcdDim);
        g.drawRect(slot, 1);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        g.drawText(names[currentPage][i], slot.removeFromTop(16), juce::Justification::centred, false);
        auto* v = processor.apvts.getRawParameterValue(ids[currentPage][i]);
        float val = v != nullptr ? v->load() : 0.0f;
        auto bar = slot.removeFromTop(14).reduced(6, 2);
        g.setColour(lcdDim);
        g.fillRect(bar);
        g.setColour(lcdLit);
        g.fillRect(bar.withWidth((int)(bar.getWidth() * val)));
        if (i == focusedSlot) {
            g.setColour(lcdLit);
            g.drawRect(slotRect(i), 2);
        }
        /* numeric readout so the knob position is actually legible */
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        g.setColour(i == focusedSlot ? lcdLit : lcdDim);
        g.drawText(juce::String::formatted("%.2f", val), slot,
                   juce::Justification::centred, false);
    }

    /* INPUT knob caption under the rightmost knob */
    auto kIn = knobRect(3);
    g.setColour(lcdDim);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
    g.drawText("INPUT", juce::Rectangle<int>(kIn.getX(), kIn.getBottom() + 2, kIn.getWidth(), 14),
               juce::Justification::centred, false);
}

void AmpNeveAudioProcessorEditor::resized() {
    auto lcd = lcdRect();
    for (int i = 0; i < 3; ++i)
        knobs[i].setBounds(knobRect(i));
    inputKnob.setBounds(knobRect(3));
    auto area = getLocalBounds();
    auto btnRow = area.removeFromBottom(34);
    bypassButton.setBounds(16, btnRow.getY(), 84, 24);
    pageButton.setBounds(108, btnRow.getY(), 84, 24);
    voiceButton.setBounds(200, btnRow.getY(), 84, 24);
    auto presetRow = area.removeFromBottom(30);
    presetBox.setBounds(presetRow.getX() + 16, presetRow.getY() + 2, 220, 24);
    saveButton.setBounds(presetRow.getX() + 248, presetRow.getY() + 2, 66, 24);
    delButton.setBounds(presetRow.getX() + 322, presetRow.getY() + 2, 62, 24);
}

void AmpNeveAudioProcessorEditor::timerCallback() {
    /* VOICE button text mirrors the active voice (Nashville/Emo) */
    if (auto* pVoice = processor.apvts.getRawParameterValue("voice")) {
        const char* txt = (pVoice->load() > 0.5f) ? "VOICE EMO" : "VOICE NAS";
        if (voiceButton.getButtonText() != juce::String(txt))
            voiceButton.setButtonText(txt);
    }
    repaint();
}

juce::File AmpNeveAudioProcessorEditor::presetDir() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("AmpNeve").getChildFile("Presets");
}

static const char* presetParamIds[11] = {
    "gain", "bass", "mid", "treble", "master", "level",
    "neve", "cab", "presence", "input", "voice"
};

void AmpNeveAudioProcessorEditor::refreshPresetList() {
    juce::ScopedValueSetter<bool> guard(loadingPresets, true);
    presetBox.clear();
    for (int i = 0; i < 5; ++i)
        presetBox.addItem(factoryPresets[i].name, i + 1);
    presetBox.addSeparator();
    userPresetFiles.clear();
    auto dir = presetDir();
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.xml");
    files.sort();
    for (int i = 0; i < files.size(); ++i) {
        userPresetFiles.add(files[i]);
        presetBox.addItem(files[i].getFileNameWithoutExtension(), 100 + i);
    }
    presetBox.setSelectedId(0, juce::dontSendNotification);
}

void AmpNeveAudioProcessorEditor::applyFactoryPreset(int index) {
    if (index < 0 || index >= 5) return;
    const FactoryPreset& p = factoryPresets[index];
    for (int i = 0; i < 11; ++i)
        if (auto* param = processor.apvts.getParameter(presetParamIds[i]))
            param->setValueNotifyingHost(p.v[i]);
    if (auto* param = processor.apvts.getParameter("bypass"))
        param->setValueNotifyingHost(0.0f);
    repaint();
}

void AmpNeveAudioProcessorEditor::saveUserPreset() {
    auto dir = presetDir();
    if (!dir.isDirectory() && !dir.createDirectory()) return;
    juce::File target;
    int sel = presetBox.getSelectedId();
    if (sel >= 100 && sel - 100 < userPresetFiles.size())
        target = userPresetFiles[sel - 100];               /* overwrite selected */
    else {
        int n = 1;
        while (dir.getChildFile(juce::String("Custom ") + juce::String(n) + ".xml").exists())
            ++n;
        target = dir.getChildFile(juce::String("Custom ") + juce::String(n) + ".xml");
    }
    auto state = processor.apvts.copyState();
    /* drop bypass from the stored state so presets always load un-bypassed */
    for (int i = state.getNumChildren() - 1; i >= 0; --i)
        if (state.getChild(i).getProperty("id").toString() == "bypass")
            state.removeChild(i, nullptr);
    state.setProperty("presetName", target.getFileNameWithoutExtension(), nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    xml->writeTo(target);
    refreshPresetList();
    int id = 100 + userPresetFiles.indexOf(target);
    if (id >= 100)
        presetBox.setSelectedId(id, juce::dontSendNotification);
}

void AmpNeveAudioProcessorEditor::deleteUserPreset() {
    int sel = presetBox.getSelectedId();
    if (sel < 100 || sel - 100 >= userPresetFiles.size()) return;
    userPresetFiles[sel - 100].deleteFile();
    refreshPresetList();
    presetBox.setSelectedId(2, juce::dontSendNotification);   /* show the default */
}

void AmpNeveAudioProcessorEditor::loadUserPreset(const juce::File& file) {
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr || !xml->hasTagName(processor.apvts.state.getType())) return;
    processor.apvts.replaceState(juce::ValueTree::fromXml(*xml));
    if (auto* param = processor.apvts.getParameter("bypass"))
        param->setValueNotifyingHost(0.0f);
    repaint();
}
