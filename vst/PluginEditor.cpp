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

AmpNeveAudioProcessorEditor::AmpNeveAudioProcessorEditor(AmpNeveAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    pageButton.setButtonText("PAGE");
    pageButton.onClick = [this] { setPage((currentPage + 1) % 3); };
    addAndMakeVisible(pageButton);

    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "bypass", bypassButton);

    for (int i = 0; i < 3; ++i) {
        knobs[i].setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knobs[i].setRange(0.0, 1.0, 0.001);
        addAndMakeVisible(knobs[i]);
    }
    knobLaf = std::make_unique<PedalKnobLookAndFeel>();
    setLookAndFeel(knobLaf.get());
    setPage(0);
    setSize(400, 380);
    startTimerHz(30);   /* keep the LCD values live while knobs move */
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
    g.drawText(juce::String("P") + juce::String(currentPage + 1), header,
               juce::Justification::centredRight, false);

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
}

void AmpNeveAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    auto lcd = lcdRect();
    /* knobs below the LCD */
    auto kArea = juce::Rectangle<int>(lcd.getX(), lcd.getBottom() + 12,
                                      lcd.getWidth(), getHeight() - lcd.getBottom() - 46);
    int w = kArea.getWidth() / 3;
    for (int i = 0; i < 3; ++i) {
        knobs[i].setBounds(kArea.getX() + i * w, kArea.getY(), w, kArea.getHeight());
    }
    pageButton.setBounds(area.getWidth() - 108, area.getHeight() - 34, 88, 24);
    bypassButton.setBounds(16, area.getHeight() - 34, 88, 24);
}

void AmpNeveAudioProcessorEditor::timerCallback() {
    repaint();
}
