#include "Detector.h"
#include "RealtimeCoefficients.h"

#include <cmath>

void Detector::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    const auto numChannels = static_cast<size_t> (spec.numChannels);

    // Built via emplace_back (matching juce::dsp::ProcessorDuplicator's own
    // internal construction, juce_ProcessorDuplicator.h) rather than
    // std::vector::assign() with a copied prototype, so every channel's
    // Filter is constructed directly from `bandpassCoefficients` without
    // relying on Filter's defaulted copy constructor to behave correctly
    // for an object that (before its own prepare() call below) may already
    // hold heap-allocated per-instance state.
    stage1.clear();
    stage2.clear();
    stage1.reserve (numChannels);
    stage2.reserve (numChannels);

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        stage1.emplace_back (bandpassCoefficients);
        stage2.emplace_back (bandpassCoefficients);
    }

    for (auto& filter : stage1)
        filter.prepare (spec);

    for (auto& filter : stage2)
        filter.prepare (spec);

    listenBuffer.setSize (static_cast<int> (numChannels), static_cast<int> (spec.maximumBlockSize));

    reset();
}

void Detector::reset()
{
    for (auto& filter : stage1)
        filter.reset();

    for (auto& filter : stage2)
        filter.reset();

    listenBuffer.clear();

    envelopeLinear = 0.0f;
    lastLevelDb = minusInfinityDb;
}

void Detector::setFrequencyAndQ (float frequencyHz, float q) noexcept
{
    const auto raw = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sampleRate, frequencyHz, q);
    lnct::applyBiquadCoefficients (*bandpassCoefficients, raw);
}

void Detector::setAttackMs (float attackMs) noexcept
{
    const auto attackSeconds = juce::jmax (0.00001, static_cast<double> (attackMs) * 0.001);
    attackCoefficient = static_cast<float> (std::exp (-1.0 / (sampleRate * attackSeconds)));
}

void Detector::setReleaseMs (float releaseMs) noexcept
{
    const auto releaseSeconds = juce::jmax (0.00001, static_cast<double> (releaseMs) * 0.001);
    releaseCoefficient = static_cast<float> (std::exp (-1.0 / (sampleRate * releaseSeconds)));
}

float Detector::processSubBlock (const juce::dsp::AudioBlock<const float>& preChainBlock,
                                  size_t startSample,
                                  size_t numSamples) noexcept
{
    if (numSamples == 0)
        return lastLevelDb;

    const auto numChannels = juce::jmin (preChainBlock.getNumChannels(), stage1.size());

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        float linkedAbs = 0.0f;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto input = preChainBlock.getSample (static_cast<int> (channel), static_cast<int> (startSample + sample));

            auto filtered = stage1[channel].processSample (input);
            filtered = stage2[channel].processSample (filtered);

            listenBuffer.setSample (static_cast<int> (channel), static_cast<int> (startSample + sample), filtered);

            linkedAbs = juce::jmax (linkedAbs, std::abs (filtered));
        }

        if (linkedAbs > envelopeLinear)
            envelopeLinear = attackCoefficient * (envelopeLinear - linkedAbs) + linkedAbs;
        else
            envelopeLinear = releaseCoefficient * (envelopeLinear - linkedAbs) + linkedAbs;
    }

    lastLevelDb = juce::Decibels::gainToDecibels (envelopeLinear, minusInfinityDb);
    return lastLevelDb;
}
