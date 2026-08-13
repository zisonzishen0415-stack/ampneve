#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <memory>

juce::AudioProcessorValueTreeState::ParameterLayout
AmpNeveAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&](const juce::String& id, const juce::String& name, float def) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), def));
    };
    /* 6 knobs, two pages x 3, pedal style: */
    add("drive", "Drive", 0.40f);
    add("tone",  "Tone",  0.50f);
    add("level", "Level", 0.80f);
    add("bass",  "Bass",  0.50f);
    add("neve",  "Neve",  0.60f);
    add("cab",   "Cab",   1.00f);
    layout.add(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    return layout;
}

AmpNeveAudioProcessor::AmpNeveAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {}

void AmpNeveAudioProcessor::prepareToPlay(double sampleRate, int) {
    if (sampleRate < 8000.0 || sampleRate > 192000.0) sampleRate = 44100.0;
    uint32_t need = Ampsim_state_size();
    stateMem.resize((need + sizeof(float) - 1u) / sizeof(float));
    core = Ampsim_init(stateMem.data(), (uint32_t)(stateMem.size() * sizeof(float)), (float)sampleRate);
    if (core != nullptr) Ampsim_reset(core);
}

void AmpNeveAudioProcessor::releaseResources() {
    core = nullptr;
    stateMem.clear();
}

void AmpNeveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    auto* pDrive = apvts.getRawParameterValue("drive");
    auto* pTone  = apvts.getRawParameterValue("tone");
    auto* pLevel = apvts.getRawParameterValue("level");
    auto* pBass  = apvts.getRawParameterValue("bass");
    auto* pNeve  = apvts.getRawParameterValue("neve");
    auto* pCab   = apvts.getRawParameterValue("cab");
    auto* pBypass = apvts.getRawParameterValue("bypass");

    if (core == nullptr) { buffer.clear(); return; }
    if (*pBypass > 0.5f) return;  /* bypass: dry passthrough */

    Ampsim_set_param(core, AMP_PARAM_DRIVE, *pDrive);
    Ampsim_set_param(core, AMP_PARAM_TONE,  *pTone);
    Ampsim_set_param(core, AMP_PARAM_LEVEL, *pLevel);
    Ampsim_set_param(core, AMP_PARAM_BASS,  *pBass);
    Ampsim_set_param(core, AMP_PARAM_NEVE,  *pNeve);
    Ampsim_set_param(core, AMP_PARAM_CAB,   *pCab);

    const int numSamples = buffer.getNumSamples();
    if ((int)monoIn.size() < numSamples) monoIn.resize(numSamples);
    const float* inL = buffer.getReadPointer(0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
    for (int i = 0; i < numSamples; ++i)
        monoIn[i] = (inR != nullptr) ? 0.5f * (inL[i] + inR[i]) : inL[i];
    const float* in = monoIn.data();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float o = 0.0f;
        Ampsim_process(core, in[i], &o);
        if (outR != nullptr) { outL[i] = o; outR[i] = o; }
        else                 { outL[i] = o; }
    }
}

bool AmpNeveAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

juce::AudioProcessorEditor* AmpNeveAudioProcessor::createEditor() {
    return new AmpNeveAudioProcessorEditor(*this);
}

void AmpNeveAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AmpNeveAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType())) {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AmpNeveAudioProcessor();
}
