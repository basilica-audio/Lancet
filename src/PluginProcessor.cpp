#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"
#include "presets/PresetManager.h"

#include <BinaryData.h>

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
        const char* autoRelease;
        const char* gainQ;
        const char* sat;
    };

    constexpr std::array<BandIdSet, LancetEngine::numBands> bandIds { {
        { ParamIDs::b1On, ParamIDs::b1Type, ParamIDs::b1Freq, ParamIDs::b1Q, ParamIDs::b1Gain,
          ParamIDs::b1Range, ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release, ParamIDs::b1Listen,
          ParamIDs::b1AutoRelease, ParamIDs::b1GainQ, ParamIDs::b1Sat },
        { ParamIDs::b2On, nullptr, ParamIDs::b2Freq, ParamIDs::b2Q, ParamIDs::b2Gain,
          ParamIDs::b2Range, ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release, ParamIDs::b2Listen,
          ParamIDs::b2AutoRelease, ParamIDs::b2GainQ, ParamIDs::b2Sat },
        { ParamIDs::b3On, nullptr, ParamIDs::b3Freq, ParamIDs::b3Q, ParamIDs::b3Gain,
          ParamIDs::b3Range, ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release, ParamIDs::b3Listen,
          ParamIDs::b3AutoRelease, ParamIDs::b3GainQ, ParamIDs::b3Sat },
        { ParamIDs::b4On, nullptr, ParamIDs::b4Freq, ParamIDs::b4Q, ParamIDs::b4Gain,
          ParamIDs::b4Range, ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release, ParamIDs::b4Listen,
          ParamIDs::b4AutoRelease, ParamIDs::b4GainQ, ParamIDs::b4Sat },
        { ParamIDs::b5On, nullptr, ParamIDs::b5Freq, ParamIDs::b5Q, ParamIDs::b5Gain,
          ParamIDs::b5Range, ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release, ParamIDs::b5Listen,
          ParamIDs::b5AutoRelease, ParamIDs::b5GainQ, ParamIDs::b5Sat },
        { ParamIDs::b6On, ParamIDs::b6Type, ParamIDs::b6Freq, ParamIDs::b6Q, ParamIDs::b6Gain,
          ParamIDs::b6Range, ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release, ParamIDs::b6Listen,
          ParamIDs::b6AutoRelease, ParamIDs::b6GainQ, ParamIDs::b6Sat },
    } };

    // The small, Lancet-specific config surface PresetManager needs (see
    // src/presets/PresetManager.h's class docs) - everything else about the
    // preset system is fully generic and portable across the suite (see
    // basilica-audio/nave's docs/preset-system-notes.md, the M2 pilot this
    // was copied from).
    basilica::presets::PresetManagerConfig makePresetManagerConfig()
    {
        // JucePlugin_CFBundleIdentifier expands to a raw (unquoted) token
        // sequence, not a string literal - JUCE_STRINGIFY() is the
        // documented way to turn it into one. Always "com.yvesvogl.lancet"
        // here (BUNDLE_ID in CMakeLists.txt), matching the "plugin" field
        // baked into every presets/factory/*.json file.
        basilica::presets::PresetManagerConfig config;
        config.pluginId = JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier);
        config.pluginName = JucePlugin_Name;
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = JucePlugin_VersionString;
        // userPresetsDirectoryOverrideForTests intentionally left
        // default-constructed (empty) - production instances always use the
        // real platform-standard preset location (see PresetManager.h).
        return config;
    }

    // BinaryData symbol names are derived from the presets/factory/*.json
    // file names passed to juce_add_binary_data() in CMakeLists.txt (dots
    // become underscores) - this list must stay in sync with that SOURCES
    // list. Order here only affects factory-preset iteration order before
    // getAllPresets() re-sorts alphabetically, so it isn't otherwise
    // significant.
    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::gentleGlue_json, BinaryData::gentleGlue_jsonSize },
            { BinaryData::deEssStack_json, BinaryData::deEssStack_jsonSize },
            { BinaryData::transientSnareCrack_json, BinaryData::transientSnareCrack_jsonSize },
            { BinaryData::mixBussSettle_json, BinaryData::mixBussSettle_jsonSize },
            { BinaryData::slowTonalRide_json, BinaryData::slowTonalRide_jsonSize },
            { BinaryData::chestResonanceTamer_json, BinaryData::chestResonanceTamer_jsonSize },
            { BinaryData::fastRecoveryDemo_json, BinaryData::fastRecoveryDemo_jsonSize },
            { BinaryData::listenCheck_json, BinaryData::listenCheck_jsonSize },
            { BinaryData::analogWarmthLift_json, BinaryData::analogWarmthLift_jsonSize },
        };
    }
}

//==============================================================================
LancetAudioProcessor::LancetAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetManager (apvts, makePresetManagerConfig(), makeFactoryPresetAssets())
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
        params.autoRelease = apvts.getRawParameterValue (ids.autoRelease);
        params.gainQ = apvts.getRawParameterValue (ids.gainQ);
        params.sat = apvts.getRawParameterValue (ids.sat);

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
        jassert (params.autoRelease != nullptr);
        jassert (params.gainQ != nullptr);
        jassert (params.sat != nullptr);
    }

    inTrimDb = apvts.getRawParameterValue (ParamIDs::inTrim);
    outTrimDb = apvts.getRawParameterValue (ParamIDs::outTrim);
    mixPercent = apvts.getRawParameterValue (ParamIDs::mix);

    jassert (inTrimDb != nullptr);
    jassert (outTrimDb != nullptr);
    jassert (mixPercent != nullptr);

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed
    // with above (see PresetManager::applyStartupDefault()'s docs).
    presetManager.applyStartupDefault();
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
        engine.setBandAutoRelease (i, params.autoRelease->load (std::memory_order_relaxed) > 0.5f);
        engine.setBandGainQ (i, params.gainQ->load (std::memory_order_relaxed) > 0.5f);
        engine.setBandSaturation (i, params.sat->load (std::memory_order_relaxed) > 0.5f);
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
