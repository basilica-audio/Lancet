#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

namespace
{
    // String IDs for one band, mirroring params/ParameterLayout.cpp's own
    // (anonymous-namespace, so not reusable directly) BandIds struct. Used
    // only to resolve APVTS atomics once at construction time below.
    struct BandIdSet
    {
        const char* on;
        const char* type; // nullptr for bands 2-5 (no Type parameter)
        const char* freq;
        const char* q;
        const char* gain;
        const char* range;
        const char* threshold;
        const char* attack;
        const char* release;
        const char* listen;
    };

    constexpr std::array<BandIdSet, LancetEngine::numBands> bandIds { {
        { ParamIDs::b1On, ParamIDs::b1Type, ParamIDs::b1Freq, ParamIDs::b1Q, ParamIDs::b1Gain,
          ParamIDs::b1Range, ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release, ParamIDs::b1Listen },
        { ParamIDs::b2On, nullptr, ParamIDs::b2Freq, ParamIDs::b2Q, ParamIDs::b2Gain,
          ParamIDs::b2Range, ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release, ParamIDs::b2Listen },
        { ParamIDs::b3On, nullptr, ParamIDs::b3Freq, ParamIDs::b3Q, ParamIDs::b3Gain,
          ParamIDs::b3Range, ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release, ParamIDs::b3Listen },
        { ParamIDs::b4On, nullptr, ParamIDs::b4Freq, ParamIDs::b4Q, ParamIDs::b4Gain,
          ParamIDs::b4Range, ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release, ParamIDs::b4Listen },
        { ParamIDs::b5On, nullptr, ParamIDs::b5Freq, ParamIDs::b5Q, ParamIDs::b5Gain,
          ParamIDs::b5Range, ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release, ParamIDs::b5Listen },
        { ParamIDs::b6On, ParamIDs::b6Type, ParamIDs::b6Freq, ParamIDs::b6Q, ParamIDs::b6Gain,
          ParamIDs::b6Range, ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release, ParamIDs::b6Listen },
    } };
}

//==============================================================================
LancetAudioProcessor::LancetAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int i = 0; i < LancetEngine::numBands; ++i)
    {
        const auto& ids = bandIds[static_cast<size_t> (i)];
        auto& params = bandParams[static_cast<size_t> (i)];

        params.on = apvts.getRawParameterValue (ids.on);
        params.type = ids.type != nullptr ? apvts.getRawParameterValue (ids.type) : nullptr;
        params.freq = apvts.getRawParameterValue (ids.freq);
        params.q = apvts.getRawParameterValue (ids.q);
        params.gain = apvts.getRawParameterValue (ids.gain);
        params.range = apvts.getRawParameterValue (ids.range);
        params.threshold = apvts.getRawParameterValue (ids.threshold);
        params.attack = apvts.getRawParameterValue (ids.attack);
        params.release = apvts.getRawParameterValue (ids.release);
        params.listen = apvts.getRawParameterValue (ids.listen);

        jassert (params.on != nullptr);
        jassert (ids.type == nullptr || params.type != nullptr);
        jassert (params.freq != nullptr);
        jassert (params.q != nullptr);
        jassert (params.gain != nullptr);
        jassert (params.range != nullptr);
        jassert (params.threshold != nullptr);
        jassert (params.attack != nullptr);
        jassert (params.release != nullptr);
        jassert (params.listen != nullptr);
    }

    inTrimDb = apvts.getRawParameterValue (ParamIDs::inTrim);
    outTrimDb = apvts.getRawParameterValue (ParamIDs::outTrim);
    mixPercent = apvts.getRawParameterValue (ParamIDs::mix);

    jassert (inTrimDb != nullptr);
    jassert (outTrimDb != nullptr);
    jassert (mixPercent != nullptr);
}

LancetAudioProcessor::~LancetAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout LancetAudioProcessor::createParameterLayout()
{
    return lnct::createParameterLayout();
}

//==============================================================================
const juce::String LancetAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LancetAudioProcessor::acceptsMidi() const
{
    return false;
}

bool LancetAudioProcessor::producesMidi() const
{
    return false;
}

bool LancetAudioProcessor::isMidiEffect() const
{
    return false;
}

double LancetAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int LancetAudioProcessor::getNumPrograms()
{
    return 1;
}

int LancetAudioProcessor::getCurrentProgram()
{
    return 0;
}

void LancetAudioProcessor::setCurrentProgram (int)
{
}

const juce::String LancetAudioProcessor::getProgramName (int)
{
    return {};
}

void LancetAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void LancetAudioProcessor::pushParametersToEngine()
{
    for (int i = 0; i < LancetEngine::numBands; ++i)
    {
        const auto& params = bandParams[static_cast<size_t> (i)];

        engine.setBandOn (i, params.on->load (std::memory_order_relaxed) > 0.5f);

        if (params.type != nullptr)
            engine.setBandShelfSelected (i, params.type->load (std::memory_order_relaxed) > 0.5f);

        engine.setBandFrequencyHz (i, params.freq->load (std::memory_order_relaxed));
        engine.setBandQ (i, params.q->load (std::memory_order_relaxed));
        engine.setBandGainDb (i, params.gain->load (std::memory_order_relaxed));
        engine.setBandRangeDb (i, params.range->load (std::memory_order_relaxed));
        engine.setBandThresholdDb (i, params.threshold->load (std::memory_order_relaxed));
        engine.setBandAttackMs (i, params.attack->load (std::memory_order_relaxed));
        engine.setBandReleaseMs (i, params.release->load (std::memory_order_relaxed));
        engine.setBandListen (i, params.listen->load (std::memory_order_relaxed) > 0.5f);
    }

    engine.setInputTrimDb (inTrimDb->load (std::memory_order_relaxed));
    engine.setOutputTrimDb (outTrimDb->load (std::memory_order_relaxed));
    engine.setMixPercent (mixPercent->load (std::memory_order_relaxed));
}

//==============================================================================
void LancetAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    // Seed the engine's parameters from the current APVTS state before
    // prepare() primes every band's filter/detector coefficients, so the
    // very first block after prepareToPlay() already reflects the host/
    // session's actual parameter values rather than the engine's built-in
    // defaults.
    pushParametersToEngine();

    engine.prepare (spec);

    // Every filter in the engine (bell/shelf bands, detector bandpasses)
    // is minimum-phase with no lookahead - Lancet never adds latency.
    setLatencySamples (engine.getLatencySamples());
}

void LancetAudioProcessor::releaseResources()
{
}

void LancetAudioProcessor::reset()
{
    engine.reset();
}

bool LancetAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != mono && mainOut != stereo)
        return false;

    if (mainOut != mainIn)
        return false;

    return true;
}

void LancetAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Buses are constrained to in == out (mono or stereo), so this is
    // normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    pushParametersToEngine();

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);
}

//==============================================================================
bool LancetAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* LancetAudioProcessor::createEditor()
{
    return new LancetAudioProcessorEditor (*this);
}

//==============================================================================
void LancetAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void LancetAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LancetAudioProcessor();
}
