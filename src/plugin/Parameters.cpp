#include "Parameters.h"

namespace so::params
{

static juce::String midiNoteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 4);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pid = [] (const char* id) { return ParameterID { id, parameterVersion }; };

    auto percent = AudioParameterFloatAttributes()
                       .withLabel ("%")
                       .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); });

    layout.add (std::make_unique<AudioParameterFloat> (pid (tension), "Tension",
                                                       NormalisableRange<float> { 0.0f, 100.0f, 1.0f }, 35.0f, percent));

    layout.add (std::make_unique<AudioParameterFloat> (pid (smoothness), "Smoothness",
                                                       NormalisableRange<float> { 0.0f, 100.0f, 1.0f }, 70.0f, percent));

    layout.add (std::make_unique<AudioParameterChoice> (pid (bassMotion), "Bass motion", bassMotionChoices, 1));

    layout.add (std::make_unique<AudioParameterInt> (pid (density), "Density", 3, 6, 4,
                                                     AudioParameterIntAttributes().withLabel ("notes")));

    auto noteAttr = AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return midiNoteName (v); });
    layout.add (std::make_unique<AudioParameterInt> (pid (rangeLow), "Range low", 24, 96, 36, noteAttr));   // C2
    layout.add (std::make_unique<AudioParameterInt> (pid (rangeHigh), "Range high", 24, 96, 84, noteAttr)); // C6

    layout.add (std::make_unique<AudioParameterBool> (pid (melodyOnTop), "Melody on top", true));

    layout.add (std::make_unique<AudioParameterFloat> (pid (previewLevel), "Preview level",
                                                       NormalisableRange<float> { -60.0f, 0.0f, 0.1f, 2.0f }, -12.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));

    for (size_t i = 0; i < colorIds.size(); ++i)
        layout.add (std::make_unique<AudioParameterBool> (pid (colorIds[i]), String ("Color: ") + colorNames[i], i < 3));

    return layout;
}

} // namespace so::params
