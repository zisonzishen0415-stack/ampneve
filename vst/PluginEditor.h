#pragma once
#include <JuceHeader.h>
#include <memory>

class AmpNeveAudioProcessor;
class PresetStrip;

/* Modern hybrid editor (G1on / MS-series look, modern plugin finish): the LCD
 * screen body stays pedal-like; knobs (ticks style), the preset strip and the
 * bottom row (BYPASS chip + page tabs) use the glacier-blue UI palette. Three
 * pages x three knobs (P1 Bass/Mid/Treble, P2 Gain/Master/Level, P3
 * Neve/Cabtype/Input) plus a live input level meter in the LCD. Cabtype
 * cycles the cabinet (2x12 / 4x12). Presets: factory + user (XML in app-data). */
class AmpNeveAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::Timer {
public:
    AmpNeveAudioProcessorEditor(AmpNeveAudioProcessor&);
    ~AmpNeveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress&) override;

    static const char* ids[3][3];
    static const char* names[3][3];

private:
    struct FactoryPreset {
        const char* name;
        float v[10];   /* gain bass mid treble master level neve cabtype input presence */
    };
    static const FactoryPreset factoryPresets[5];

    void setPage(int page);
    void setFocus(int slot);
    void mouseDown(const juce::MouseEvent&) override;
    void refreshPresetList();
    void applyFactoryPreset(int index);
    void saveUserPreset();
    void deleteUserPreset();
    void loadUserPreset(const juce::File& file);
    void cyclePreset(int dir);
    juce::File presetDir() const;

    juce::Rectangle<int> lcdRect() const;
    juce::Rectangle<int> slotRect(int slot) const;
    juce::Rectangle<int> knobRect(int index) const;
    int focusedSlot = 0;
    juce::Array<juce::File> userPresetFiles;

    AmpNeveAudioProcessor& processor;
    juce::Slider knobs[3];
    juce::Slider inputKnob;               /* top small knob: input trim */
    std::unique_ptr<PresetStrip> presetStrip;
    juce::ToggleButton bypassButton{"BYPASS"};
    juce::ToggleButton pageTabs[3];
    juce::Rectangle<int> pageHitRect;   /* LCD header module-name hit zone */
    std::unique_ptr<juce::ComponentBoundsConstrainer> resizeConstrainer;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::LookAndFeel> appLaf;
    int currentPage = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpNeveAudioProcessorEditor)
};
