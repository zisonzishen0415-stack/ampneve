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
    /* 9 knobs, three pages x 3 (tone / amp / voicing), pedal style: */
    add("gain",     "Gain",     0.45f);
    add("bass",     "Bass",     0.50f);
    add("mid",      "Mid",      0.50f);
    add("treble",   "Treble",   0.50f);
    add("master",   "Master",   0.50f);
    add("level",    "Level",    0.80f);
    add("neve",     "Neve",     1.00f);
    add("cab",      "Cab",      0.50f);
    add("presence", "Presence", 1.00f);
    add("input", "Input", 1.00f);
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
    auto* pGain     = apvts.getRawParameterValue("gain");
    auto* pBass     = apvts.getRawParameterValue("bass");
    auto* pMid      = apvts.getRawParameterValue("mid");
    auto* pTreble   = apvts.getRawParameterValue("treble");
    auto* pMaster   = apvts.getRawParameterValue("master");
    auto* pLevel    = apvts.getRawParameterValue("level");
    auto* pNeve     = apvts.getRawParameterValue("neve");
    auto* pCab      = apvts.getRawParameterValue("cab");
    auto* pPresence = apvts.getRawParameterValue("presence");
    auto* pInput    = apvts.getRawParameterValue("input");
    auto* pBypass   = apvts.getRawParameterValue("bypass");

    if (core == nullptr) { buffer.clear(); return; }
    if (*pBypass > 0.5f) return;  /* bypass: dry passthrough */

    Ampsim_set_param(core, AMP_PARAM_GAIN,   *pGain);
    Ampsim_set_param(core, AMP_PARAM_BASS,   *pBass);
    Ampsim_set_param(core, AMP_PARAM_MID,    *pMid);
    Ampsim_set_param(core, AMP_PARAM_TREBLE, *pTreble);
    Ampsim_set_param(core, AMP_PARAM_MASTER, *pMaster);
    Ampsim_set_param(core, AMP_PARAM_LEVEL,  *pLevel);
    Ampsim_set_param(core, AMP_PARAM_NEVE,    *pNeve);
    Ampsim_set_param(core, AMP_PARAM_CAB,     *pCab);
    Ampsim_set_param(core, AMP_PARAM_PRESENCE, *pPresence);
    Ampsim_set_param(core, AMP_PARAM_INPUT, *pInput);

    const int numSamples = buffer.getNumSamples();
    if ((int)monoIn.size() < numSamples) monoIn.resize(numSamples);
    const float* inL = buffer.getReadPointer(0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
    for (int i = 0; i < numSamples; ++i)
        monoIn[i] = (inR != nullptr) ? 0.5f * (inL[i] + inR[i]) : inL[i];
    const float* in = monoIn.data();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float av = in[i] < 0.0f ? -in[i] : in[i];
        if (av > blockPeak) blockPeak = av;
        float o = 0.0f;
        Ampsim_process(core, in[i], &o);
        if (outR != nullptr) { outL[i] = o; outR[i] = o; }
        else                 { outL[i] = o; }
    }
    /* input meter (raw DAW input, pre-trim): fast attack, ~250 ms release;
     * peak hold decays slowly. Editor colors vs the -12..-6 dBFS DI target. */
    {
        float cur = inMeter.load(std::memory_order_relaxed);
        float c = (blockPeak > cur) ? 0.45f : 0.06f;
        inMeter.store(cur + (blockPeak - cur) * c, std::memory_order_relaxed);
        float pk = inMeterPeak.load(std::memory_order_relaxed);
        if (blockPeak > pk) pk = blockPeak;
        else                pk *= 0.997f;
        inMeterPeak.store(pk, std::memory_order_relaxed);
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
