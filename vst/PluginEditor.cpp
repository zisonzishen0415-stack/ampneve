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
    addAndMakeVisible(voiceButton);
    voiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "voice", voiceButton);

    for (int i = 0; i < 3; ++i) {
        knobs[i].setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knobs[i].setRange(0.0, 1.0, 0.001);
        addAndMakeVisible(knobs[i]);
    }
    /* dedicated Input knob (hardware INPUT VOL), outside the page knobs */
    inputKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    inputKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    inputKnob.setRange(0.0, 1.0, 0.001);
    addAndMakeVisible(inputKnob);
    inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "input", inputKnob);

    knobLaf = std::make_unique<PedalKnobLookAndFeel>();
    setLookAndFeel(knobLaf.get());
    setPage(0);
    setSize(400, 380);
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
    auto kArea = juce::Rectangle<int>(lcd.getX(), lcd.getBottom() + 12,
                                      lcd.getWidth(), getHeight() - lcd.getBottom() - 46);
    int w = kArea.getWidth() / 4;
    return juce::Rectangle<int>(kArea.getX() + index * w, kArea.getY(), w, kArea.getHeight());
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
    g.drawText("AMPNEVE", header.removeFromLeft(180), juce::Justification::centredLeft, false);
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
    }

    /* INPUT knob caption under the rightmost knob */
    auto kIn = knobRect(3);
    g.setColour(lcdDim);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
    g.drawText("INPUT", juce::Rectangle<int>(kIn.getX(), getHeight() - 30, kIn.getWidth(), 18),
               juce::Justification::centred, false);
}

void AmpNeveAudioProcessorEditor::resized() {
    auto lcd = lcdRect();
    for (int i = 0; i < 3; ++i)
        knobs[i].setBounds(knobRect(i));
    inputKnob.setBounds(knobRect(3));
    auto area = getLocalBounds();
    bypassButton.setBounds(16, area.getHeight() - 34, 84, 24);
    pageButton.setBounds(108, area.getHeight() - 34, 84, 24);
    voiceButton.setBounds(200, area.getHeight() - 34, 84, 24);
}

void AmpNeveAudioProcessorEditor::timerCallback() {
    repaint();
}
