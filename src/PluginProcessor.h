#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/LancetEngine.h"

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

    juce::AudioProcessorValueTreeState apvts;

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
    };

    std::array<BandParams, LancetEngine::numBands> bandParams;

    std::atomic<float>* inTrimDb = nullptr;
    std::atomic<float>* outTrimDb = nullptr;
    std::atomic<float>* mixPercent = nullptr;

    // Reads every APVTS atomic and pushes the current values into `engine`.
    // Called both from prepareToPlay() (so the first block after prepare
    // already reflects the host/session's actual parameter values) and
    // from every processBlock() call.
    void pushParametersToEngine();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LancetAudioProcessor)
};
