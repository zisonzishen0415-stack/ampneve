#pragma once
#include <JuceHeader.h>
#include "ampsim.h"
#include <vector>
#include <atomic>
#include <cstddef>

class AmpNeveAudioProcessor : public juce::AudioProcessor {
public:
    AmpNeveAudioProcessor();
    ~AmpNeveAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AmpNeve"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /* input level meter (audio thread -> editor): smoothed raw-input peak
     * and a slowly-decaying peak hold, linear 0..1 (read by the editor). */
    std::atomic<float> inMeter{0.0f};
    std::atomic<float> inMeterPeak{0.0f};

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::vector<float> stateMem;   /* owns core state memory */
    std::vector<float> monoIn;     /* stereo input downmix scratch */
    Ampsim* core = nullptr;
};
