#include "PluginProcessor.h"
#include "PluginEditor.h"

SecondOpinionProcessor::SecondOpinionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", so::params::createLayout())
{
    session.setProperty ("stateVersion", stateVersion, nullptr);
}

void SecondOpinionProcessor::prepareToPlay (double, int)
{
    setLatencySamples (0); // NFR-03: all analysis is offline, the audio path adds nothing.
}

bool SecondOpinionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void SecondOpinionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Real-time safe: no allocation, no locks, no I/O. Audio passes through untouched (bit-identical).
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            transport.playing.store (pos->getIsPlaying(), std::memory_order_relaxed);

            if (const auto bpm = pos->getBpm())
            {
                transport.bpm.store (*bpm, std::memory_order_relaxed);
                transport.hasTempo.store (true, std::memory_order_relaxed);
            }

            if (const auto ppq = pos->getPpqPosition())
                transport.ppq.store (*ppq, std::memory_order_relaxed);

            if (const auto ts = pos->getTimeSignature())
            {
                transport.numerator.store (ts->numerator, std::memory_order_relaxed);
                transport.denominator.store (ts->denominator, std::memory_order_relaxed);
            }
        }
    }
}

juce::AudioProcessorEditor* SecondOpinionProcessor::createEditor()
{
    return new SecondOpinionEditor (*this);
}

void SecondOpinionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("SecondOpinionState");
    root.setProperty ("stateVersion", stateVersion, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (session.createCopy(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void SecondOpinionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Must survive garbage input (pluginval fuzzes this): validate every step, never assert.
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("SecondOpinionState"))
        return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid())
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    auto loaded = root.getChildWithName ("Session");
    if (loaded.isValid())
    {
        auto copy = loaded.createCopy();
        migrateSession (copy);
        session.copyPropertiesAndChildrenFrom (copy, nullptr);
    }
}

void SecondOpinionProcessor::migrateSession (juce::ValueTree& tree)
{
    const int from = tree.getProperty ("stateVersion", 0);

    // Version 0 → 1: nothing stored yet, only stamp the version. Future migrations chain here.
    if (from < 1)
        tree.setProperty ("stateVersion", 1, nullptr);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SecondOpinionProcessor();
}
