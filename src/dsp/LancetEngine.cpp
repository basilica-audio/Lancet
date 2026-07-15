#include "LancetEngine.h"

LancetEngine::LancetEngine() = default;

void LancetEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    inputTrim.setRampDurationSeconds (smoothingTimeSeconds);
    inputTrim.prepare (spec);
    outputTrim.setRampDurationSeconds (smoothingTimeSeconds);
    outputTrim.prepare (spec);

    // juce::dsp::Gain<float>::prepare() resets its internal SmoothedValue
    // to whatever target was last set (0 dB/unity if setGainDecibels() was
    // never called - see this class's own prepare() flow above, which does
    // call setInputTrimDb()/setOutputTrimDb() before this prepare() during
    // normal LancetAudioProcessor usage). Re-applying the last commanded
    // value here and then re-resetting keeps LancetEngine correct even when
    // used directly with default trim (no setter call before prepare()) -
    // see lastInputTrimDb/lastOutputTrimDb's doc comment in LancetEngine.h.
    inputTrim.setGainDecibels (lastInputTrimDb);
    inputTrim.reset();
    outputTrim.setGainDecibels (lastOutputTrimDb);
    outputTrim.reset();

    for (auto* band : bands)
        band->prepare (spec);

    preChainBuffer.setSize (static_cast<int> (spec.numChannels), static_cast<int> (spec.maximumBlockSize));

    dryWetMixer.prepare (spec);

    // juce::dsp::DryWetMixer defaults its internal mix to fully wet (1.0)
    // until told otherwise, and its own reset() (called from our reset()
    // below) snaps its internal dry/wet gain smoothers' *current* value to
    // whatever their *target* happens to be at that moment - it does not
    // know about lastMixProportion. Priming the real target here, before
    // reset() runs, means the mixer is already sitting at the correct
    // dry/wet balance for the very first process() call instead of ramping
    // up from "fully wet" over its internal 50ms default ramp. Same
    // pattern as sibling plugin overture's OvertureEngine.cpp (see this
    // repo's CLAUDE.md "DryWetMixer gotcha" note).
    dryWetMixer.setWetMixProportion (lastMixProportion);

    reset();
}

void LancetEngine::reset()
{
    inputTrim.reset();
    outputTrim.reset();
    dryWetMixer.reset();

    for (auto* band : bands)
        band->reset();

    preChainBuffer.clear();
}

void LancetEngine::setBandOn (int bandIndex, bool isOn) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setOn (isOn);
}

void LancetEngine::setBandShelfSelected (int bandIndex, bool useShelf) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setShelfSelected (useShelf);
}

void LancetEngine::setBandFrequencyHz (int bandIndex, float frequencyHz) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setFrequencyHz (frequencyHz);
}

void LancetEngine::setBandQ (int bandIndex, float q) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setQ (q);
}

void LancetEngine::setBandGainDb (int bandIndex, float gainDb) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setStaticGainDb (gainDb);
}

void LancetEngine::setBandRangeDb (int bandIndex, float rangeDb) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setRangeDb (rangeDb);
}

void LancetEngine::setBandThresholdDb (int bandIndex, float thresholdDb) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setThresholdDb (thresholdDb);
}

void LancetEngine::setBandAttackMs (int bandIndex, float attackMs) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setAttackMs (attackMs);
}

void LancetEngine::setBandReleaseMs (int bandIndex, float releaseMs) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setReleaseMs (releaseMs);
}

void LancetEngine::setBandListen (int bandIndex, bool listen) noexcept
{
    jassert (bandIndex >= 0 && bandIndex < numBands);
    bands[static_cast<size_t> (bandIndex)]->setListen (listen);
}

void LancetEngine::setInputTrimDb (float newTrimDb) noexcept
{
    lastInputTrimDb = newTrimDb;
    inputTrim.setGainDecibels (newTrimDb);
}

void LancetEngine::setOutputTrimDb (float newTrimDb) noexcept
{
    lastOutputTrimDb = newTrimDb;
    outputTrim.setGainDecibels (newTrimDb);
}

void LancetEngine::setMixPercent (float newMixPercent) noexcept
{
    lastMixProportion = juce::jlimit (0.0f, 1.0f, newMixPercent * 0.01f);
    dryWetMixer.setWetMixProportion (lastMixProportion);
}

void LancetEngine::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto requestedSamples = block.getNumSamples();

    if (requestedSamples == 0)
        return;

    // Defensive: clamp to the pre-chain buffer capacity established in
    // prepare(), in case a host ever calls process() with more samples (or
    // channels) than it promised via prepareToPlay() - trimming the
    // working block rather than writing out of bounds.
    const auto numSamples = juce::jmin (requestedSamples, static_cast<size_t> (preChainBuffer.getNumSamples()));
    const auto numChannels = juce::jmin (block.getNumChannels(), static_cast<size_t> (preChainBuffer.getNumChannels()));

    if (numSamples == 0 || numChannels == 0)
        return;

    auto workingBlock = block.getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

    juce::dsp::ProcessContextReplacing<float> inputTrimContext (workingBlock);
    inputTrim.process (inputTrimContext);

    // Dry tap for the Mix blend: right after Input Trim, before Band 1, so
    // Input Trim always applies equally to both the dry and wet paths (see
    // LancetEngine.h's class comment).
    const juce::dsp::AudioBlock<const float> dryBlock (workingBlock);
    dryWetMixer.pushDrySamples (dryBlock);

    // Pre-chain tap for every band's Detector: a copy of the same
    // post-Input-Trim signal, taken once here rather than per band, so all
    // six Detectors see an identical, unperturbed source regardless of
    // what upstream bands do to `workingBlock`.
    auto preChainBlock = juce::dsp::AudioBlock<float> (preChainBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    preChainBlock.copyFrom (workingBlock);
    const juce::dsp::AudioBlock<const float> preChainBlockConst (preChainBlock);

    // Coefficient/gain updates happen once per <= 32-sample sub-block (see
    // class comment); the main serial filter chain itself still processes
    // every sample within that sub-block.
    size_t position = 0;

    while (position < numSamples)
    {
        const auto subSize = juce::jmin (subBlockSize, numSamples - position);
        auto subBlock = workingBlock.getSubBlock (position, subSize);

        for (auto* band : bands)
            band->processSubBlock (subBlock, preChainBlockConst, position, subSize);

        position += subSize;
    }

    // Listen (exclusive sidechain solo): the lowest-indexed band with
    // Listen engaged wins - deterministic if more than one is somehow
    // engaged simultaneously (the GUI itself behaves like a radio group).
    // Substitutes this whole block's worth of program output with that
    // band's own bandpass-filtered detector signal, still subject to Mix
    // and Output Trim below.
    for (auto* band : bands)
    {
        if (! band->isListening())
            continue;

        const juce::dsp::AudioBlock<const float> listenBlock (band->getListenBuffer());
        workingBlock.copyFrom (listenBlock.getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels));
        break;
    }

    dryWetMixer.mixWetSamples (workingBlock);

    juce::dsp::ProcessContextReplacing<float> outputTrimContext (workingBlock);
    outputTrim.process (outputTrimContext);
}
