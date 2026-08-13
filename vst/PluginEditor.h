#pragma once
#include <JuceHeader.h>
#include <memory>

class AmpNeveAudioProcessor;

/* Pedal-style three-page x three-knob editor (G1on / MS-series look):
 * P1 Bass/Mid/Treble, P2 Gain/Master/Level, P3 Neve/Cab/Presence. */
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
    void setPage(int page);
    void setFocus(int slot);
    void mouseDown(const juce::MouseEvent&) override;

    juce::Rectangle<int> lcdRect() const;
    juce::Rectangle<int> slotRect(int slot) const;
    int focusedSlot = 0;

    AmpNeveAudioProcessor& processor;
    juce::Slider knobs[3];
    juce::TextButton pageButton{"PAGE"};
    juce::TextButton bypassButton{"BYPASS"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::LookAndFeel> knobLaf;
    int currentPage = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpNeveAudioProcessorEditor)
};
