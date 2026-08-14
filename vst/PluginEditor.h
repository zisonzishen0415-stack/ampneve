#pragma once
#include <JuceHeader.h>
#include <memory>

class AmpNeveAudioProcessor;

/* Pedal-style editor (G1on / MS-series look): three pages x three knobs
 * (P1 Bass/Mid/Treble, P2 Gain/Master/Level, P3 Neve/Cabtype/Input) plus a
 * live input level meter in the LCD. Cabtype cycles the cabinet
 * (1x12 / 2x12 / 4x12). A preset row (factory + user presets) lives above
 * the pedal buttons: user presets persist as XML files in the OS app-data
 * dir, so settings survive DAW sessions even if the host does not restore
 * plugin state. */
class AmpNeveAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::Timer {
public:
    AmpNeveAudioProcessorEditor(AmpNeveAudioProcessor&);
    ~AmpNeveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    static const char* ids[3][3];
    static const char* names[3][3];

private:
    struct FactoryPreset {
        const char* name;
        float v[9];   /* gain bass mid treble master level neve cabtype input */
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
    juce::File presetDir() const;

    juce::Rectangle<int> lcdRect() const;
    juce::Rectangle<int> slotRect(int slot) const;
    juce::Rectangle<int> knobRect(int index) const;
    int focusedSlot = 0;
    bool loadingPresets = false;
    juce::Array<juce::File> userPresetFiles;

    AmpNeveAudioProcessor& processor;
    juce::Slider knobs[3];
    juce::TextButton pageButton{"PAGE"};
    juce::TextButton bypassButton{"BYPASS"};
    juce::ComboBox presetBox;
    juce::TextButton saveButton{"SAVE"};
    juce::TextButton delButton{"DEL"};
    std::unique_ptr<juce::ComponentBoundsConstrainer> resizeConstrainer;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::LookAndFeel> knobLaf;
    int currentPage = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpNeveAudioProcessorEditor)
};
