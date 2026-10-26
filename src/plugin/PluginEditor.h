#pragma once

#include "PluginProcessor.h"
#include "bridge/NativeDragStrip.h"

#include <juce_gui_extra/juce_gui_extra.h>

/** One WebView (the whole UI, served offline from BinaryData) + the native DragStrip underneath.
    Resizable 1100×700 by default, 860×560 to 1800×1100 (UI brief, Window and layout). */
class SecondOpinionEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit SecondOpinionEditor (SecondOpinionProcessor&);
    ~SecondOpinionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultWidth = 1100, defaultHeight = 700;
    static constexpr int minWidth = 860, minHeight = 560;
    static constexpr int maxWidth = 1800, maxHeight = 1100;
    static constexpr int dragStripHeight = 48;

private:
    void timerCallback() override;
    juce::WebBrowserComponent::Options createWebOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    SecondOpinionProcessor& processor;

    // Relays must be constructed before the WebBrowserComponent that registers them.
    juce::WebSliderRelay tensionRelay { so::params::tension };
    juce::WebSliderRelay smoothnessRelay { so::params::smoothness };
    juce::WebSliderRelay densityRelay { so::params::density };
    juce::WebSliderRelay rangeLowRelay { so::params::rangeLow };
    juce::WebSliderRelay rangeHighRelay { so::params::rangeHigh };
    juce::WebSliderRelay previewLevelRelay { so::params::previewLevel };
    juce::WebComboBoxRelay bassMotionRelay { so::params::bassMotion };
    juce::WebToggleButtonRelay melodyOnTopRelay { so::params::melodyOnTop };
    std::vector<std::unique_ptr<juce::WebToggleButtonRelay>> colorRelays;

    NativeDragStrip dragStrip;
    std::unique_ptr<juce::WebBrowserComponent> web;
    juce::Label fallbackMessage;

    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> sliderAttachments;
    std::unique_ptr<juce::WebComboBoxParameterAttachment> comboAttachment;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> toggleAttachments;

    juce::ComponentBoundsConstrainer constrainer;

    struct LastTransport { bool playing = false; double bpm = 0; double ppq = -1; int num = 0, den = 0; bool hasTempo = false; } lastSent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecondOpinionEditor)
};
