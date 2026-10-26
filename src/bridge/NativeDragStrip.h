#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

/** The 48 px bar at the bottom of the editor (UI brief, DragStrip). It is a native JUCE component,
    not part of the WebView, because starting an OS-level file drag from inside a WebView is
    unreliable across hosts (PRD §7.6, spike P1-05).

    Each handle writes a temp .mid (SMF type 1, 960 or 480 PPQ, tempo + time signature) for the current
    progression and starts performExternalDragDropOfFiles. */
class NativeDragStrip final : public juce::Component,
                              public juce::DragAndDropContainer
{
public:
    explicit NativeDragStrip (SecondOpinionProcessor&);
    ~NativeDragStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Call after the processor's progression changes. */
    void refresh();

    /** Opens a folder chooser and writes every lane (FR-OUT-02). */
    void exportToFolder();

    enum class Lane { Chords, Bass, Top, All };

    /** Writes the lane's MIDI to `file`. Returns false when there is nothing to write. */
    bool writeLane (Lane lane, const juce::File& file) const;

    /** SMF bytes for a lane of the processor's current progression, honouring the MIDI export
        settings (FR-SET-02): PPQ 480 or 960, and flat (all 100) or humanized velocities (the
        voicer's accents plus a small variation seeded by the progression id, so the same
        progression always writes the same bytes). Empty when there is nothing to write. */
    static std::vector<std::uint8_t> laneBytes (SecondOpinionProcessor&, Lane lane);

    static juce::String fileNameFor (const juce::String& suggestionTitle, Lane lane);

    /** Deletes drag folders older than an hour (other instances may still be dragging newer ones). */
    static void cleanUpOldDrags();

private:
    class DragHandle;
    class ExportButton;

    SecondOpinionProcessor& processor;
    std::vector<std::unique_ptr<DragHandle>> handles;
    std::unique_ptr<ExportButton> exportButton;
    juce::Slider previewLevel;
    juce::Label previewLabel;
    std::unique_ptr<juce::SliderParameterAttachment> previewAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeDragStrip)
};
