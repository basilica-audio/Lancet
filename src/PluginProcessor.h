#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/LancetEngine.h"
#include "presets/PresetManager.h"

#include <array>

// Lancet: a six-band surgical dynamic EQ for heavy mixes. Signal flow lives
// in LancetEngine (src/dsp) so it stays unit-testable independent of this
// AudioProcessor; this class is just APVTS + host plumbing around it.
class LancetAudioProcessor final : public juce::AudioProcessor
{
public:
    LancetAudioProcessor();
    ~LancetAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    // Saved-state schema version (v0.4.0). getStateInformation() stamps this
    // as a `stateVersion` attribute on the exported state root; an XML
    // without the attribute is schema 1, i.e. anything Lancet v0.1.0-v0.3.0
    // ever wrote.
    //
    // Schema 2 needs no value remapping: every parameter added since schema
    // 1 defaults to its pre-existing behaviour, so JUCE's own tolerant
    // restore (absent ID -> ParameterLayout default) already produces the
    // right result. The attribute is written now purely so that a future
    // release which *does* need a transform (e.g. widening an existing
    // NormalisableRange, which changes host automation-curve mapping) has a
    // reliable way to tell which schema it is reading.
    static constexpr int currentStateVersion = 2;

    // Attribute name the version is stamped under on the exported state root.
    static constexpr const char* stateVersionAttribute = "stateVersion";

    // The schema version of the most recent setStateInformation() call, or
    // currentStateVersion if no state has been restored yet. Read by
    // tests/StateTests.cpp; nothing in the audio path consults it.
    int getLoadedStateVersion() const noexcept { return loadedStateVersion; }

    juce::AudioProcessorValueTreeState apvts;

    // M2 preset system (.scaffold/specs/preset-system-m2.md,
    // src/presets/PresetManager.h). Constructed after apvts (its
    // constructor registers APVTS parameter listeners) and public so
    // LancetAudioProcessorEditor's PresetBar can talk to it directly - the
    // same "processor owns it, editor references it" pattern apvts itself
    // already uses.
    basilica::presets::PresetManager presetManager;

private:
    LancetEngine engine;

    // Raw atomic pointers into the APVTS-managed parameter values for one
    // band, resolved once at construction time so processBlock() never has
    // to search for them (no allocation/locks on the audio thread). `type`
    // is nullptr for bands 2-5, which have no Type parameter at all (see
    // ParameterIds.h) - pushParametersToEngine() skips those.
    struct BandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* range = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* listen = nullptr;
        std::atomic<float>* autoRelease = nullptr;
        std::atomic<float>* gainQ = nullptr;
        std::atomic<float>* sat = nullptr;
        std::atomic<float>* scSource = nullptr;
        std::atomic<float>* scMode = nullptr;
    };

    std::array<BandParams, LancetEngine::numBands> bandParams;

    std::atomic<float>* inTrimDb = nullptr;
    std::atomic<float>* outTrimDb = nullptr;
    std::atomic<float>* mixPercent = nullptr;

    // See getLoadedStateVersion(). Message-thread only.
    int loadedStateVersion = currentStateVersion;

    // Reads every APVTS atomic and pushes the current values into `engine`.
    // Called both from prepareToPlay() (so the first block after prepare
    // already reflects the host/session's actual parameter values) and
    // from every processBlock() call.
    void pushParametersToEngine();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LancetAudioProcessor)
};
