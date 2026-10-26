#include "NativeDragStrip.h"

#include "core/midi/MidiWriter.h"
#include "plugin/Theme.h"

namespace
{
    juce::File dragRoot()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("SecondOpinion")
            .getChildFile ("drag");
    }

    juce::String laneLabel (NativeDragStrip::Lane lane)
    {
        switch (lane)
        {
            case NativeDragStrip::Lane::Chords: return "Chords";
            case NativeDragStrip::Lane::Bass:   return "Bass";
            case NativeDragStrip::Lane::Top:    return "Top line";
            case NativeDragStrip::Lane::All:    return "All";
        }
        return {};
    }

    juce::String sanitise (juce::String s)
    {
        s = s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
        return s.trim().replaceCharacter (' ', '-').substring (0, 48);
    }
}

//==============================================================================
class NativeDragStrip::DragHandle final : public juce::Component
{
public:
    DragHandle (NativeDragStrip& o, Lane l) : owner (o), lane (l)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTitle (laneLabel (lane) + " MIDI");
        setDescription ("Drag into your DAW");
        setWantsKeyboardFocus (false);
    }

    void refresh (const so::midi::VoicedLanes* lanes)
    {
        preview.clear();
        if (lanes != nullptr)
        {
            auto add = [this] (const so::midi::MidiLane& ml) { for (auto& n : ml.notes) preview.push_back (n); };
            if (lane == Lane::Chords || lane == Lane::All) add (lanes->chords);
            if (lane == Lane::Bass || lane == Lane::All)   add (lanes->bass);
            if (lane == Lane::Top)                         add (lanes->top);
        }
        setEnabled (! preview.empty());
        setTooltipText();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace so::theme;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool enabled = isEnabled();

        g.setColour (dragging ? surface3 : (hover && enabled ? surface3 : surface2));
        g.fillRoundedRectangle (r, radiusMd);
        g.setColour (dragging ? accent : (hover && enabled ? borderStrong : border));
        g.drawRoundedRectangle (r, radiusMd, 1.0f);

        const float alpha = enabled ? 1.0f : 0.4f;
        auto content = getLocalBounds().reduced (10, 0);

        // Grip: 2 × 3 dots
        auto grip = content.removeFromLeft (8).toFloat();
        g.setColour (text3.withMultipliedAlpha (alpha));
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse (grip.getX() + (float) col * 4.0f, grip.getCentreY() - 5.0f + (float) row * 4.0f, 2.0f, 2.0f);

        content.removeFromLeft (8);

        // Mini lane preview on the right
        auto previewArea = content.removeFromRight (juce::jmin (64, content.getWidth() / 2)).reduced (0, 12).toFloat();
        drawPreview (g, previewArea, alpha);

        content.removeFromRight (8);
        g.setColour ((enabled ? text : text3));
        g.setFont (juce::FontOptions (13.0f, juce::Font::plain));
        g.drawFittedText (hover && enabled && ! dragging ? juce::String ("Drag to DAW") : laneLabel (lane),
                          content, juce::Justification::centredLeft, 1);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || ! isEnabled() || e.getDistanceFromDragStart() < 4)
            return;

        const auto dir = dragRoot().getChildFile (juce::Uuid().toString());
        const auto file = dir.getChildFile (NativeDragStrip::fileNameFor (owner.processor.progression.title, lane));

        if (! dir.createDirectory() || ! owner.writeLane (lane, file))
            return;

        dragging = true;
        repaint();

        juce::Component::SafePointer<DragHandle> safe (this);
        owner.performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [safe]
        {
            if (safe != nullptr)
            {
                safe->dragging = false;
                safe->repaint();
            }
        });
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        repaint();
    }

private:
    void setTooltipText() { setHelpText (isEnabled() ? "Drag into your DAW" : "Pick a suggestion first"); }

    void drawPreview (juce::Graphics& g, juce::Rectangle<float> area, float alpha)
    {
        using namespace so::theme;
        if (preview.empty())
        {
            g.setColour (border.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (area.withHeight (2.0f).withCentre (area.getCentre()), 1.0f);
            return;
        }

        double end = 0.0;
        int lo = 127, hi = 0;
        for (const auto& n : preview)
        {
            end = std::max (end, n.startBeat + n.lengthBeats);
            lo = std::min (lo, n.pitch);
            hi = std::max (hi, n.pitch);
        }

        const float span = (float) juce::jmax (1, hi - lo);
        g.setColour ((lane == Lane::Bass ? original : accent).withMultipliedAlpha (0.85f * alpha));

        for (const auto& n : preview)
        {
            const float x = area.getX() + area.getWidth() * (float) (n.startBeat / end);
            const float w = juce::jmax (1.5f, area.getWidth() * (float) (n.lengthBeats / end) - 1.0f);
            const float y = area.getBottom() - 2.0f - (area.getHeight() - 2.0f) * ((float) (n.pitch - lo) / span);
            g.fillRoundedRectangle (x, y - 1.0f, w, 2.0f, 1.0f);
        }
    }

    NativeDragStrip& owner;
    Lane lane;
    bool hover { false }, dragging { false };
    std::vector<so::midi::MidiNote> preview;
};

//==============================================================================
class NativeDragStrip::ExportButton final : public juce::Button
{
public:
    ExportButton() : juce::Button (juce::String::fromUTF8 ("Export MIDI\xe2\x80\xa6")) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace so::theme;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? surface3 : (over ? surface2 : juce::Colours::transparentBlack));
        g.fillRoundedRectangle (r, radiusMd);
        g.setColour (over ? borderStrong : border);
        g.drawRoundedRectangle (r, radiusMd, 1.0f);
        g.setColour (isEnabled() ? so::theme::text : so::theme::text3);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
NativeDragStrip::NativeDragStrip (SecondOpinionProcessor& p) : processor (p)
{
    for (auto lane : { Lane::Chords, Lane::Bass, Lane::Top, Lane::All })
    {
        handles.push_back (std::make_unique<DragHandle> (*this, lane));
        addAndMakeVisible (*handles.back());
    }

    exportButton = std::make_unique<ExportButton>();
    exportButton->onClick = [this] { exportToFolder(); };
    addAndMakeVisible (*exportButton);

    using namespace so::theme;
    previewLevel.setSliderStyle (juce::Slider::LinearHorizontal);
    previewLevel.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    previewLevel.setColour (juce::Slider::trackColourId, accent);
    previewLevel.setColour (juce::Slider::backgroundColourId, surface3);
    previewLevel.setColour (juce::Slider::thumbColourId, text);
    previewLevel.setTitle ("Preview level");
    addAndMakeVisible (previewLevel);

    previewLabel.setText ("Preview", juce::dontSendNotification);
    previewLabel.setFont (juce::FontOptions (12.0f));
    previewLabel.setColour (juce::Label::textColourId, text2);
    previewLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (previewLabel);

    if (auto* param = processor.apvts.getParameter (so::params::previewLevel))
        previewAttachment = std::make_unique<juce::SliderParameterAttachment> (*param, previewLevel);

    cleanUpOldDrags();
    refresh();
}

NativeDragStrip::~NativeDragStrip() = default;

void NativeDragStrip::paint (juce::Graphics& g)
{
    using namespace so::theme;
    g.fillAll (surface1);
    g.setColour (border);
    g.fillRect (0, 0, getWidth(), 1);
}

void NativeDragStrip::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (1);

    auto right = r.removeFromRight (juce::jmin (300, r.getWidth() / 3));
    previewLabel.setBounds (right.removeFromLeft (64));
    right.removeFromLeft (8);
    previewLevel.setBounds (right.removeFromLeft (juce::jmax (60, right.getWidth() - 0)));

    r.removeFromRight (16);
    exportButton->setBounds (r.removeFromRight (118));
    r.removeFromRight (16);

    const int gap = 8;
    const int w = (r.getWidth() - gap * 3) / 4;
    for (auto& h : handles)
    {
        h->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (gap);
    }
}

void NativeDragStrip::refresh()
{
    const bool has = ! processor.progression.chords.empty();
    so::midi::VoicedLanes lanes;
    if (has)
        lanes = so::midi::voiceProgression (processor.progression.chords);

    for (auto& h : handles)
        h->refresh (has ? &lanes : nullptr);

    exportButton->setEnabled (has);
}

bool NativeDragStrip::writeLane (Lane lane, const juce::File& file) const
{
    const auto& prog = processor.progression;
    if (prog.chords.empty())
        return false;

    const auto voiced = so::midi::voiceProgression (prog.chords);
    std::vector<so::midi::MidiLane> lanes;

    switch (lane)
    {
        case Lane::Chords: lanes = { voiced.chords }; break;
        case Lane::Bass:   lanes = { voiced.bass }; break;
        case Lane::Top:    lanes = { voiced.top }; break;
        case Lane::All:    lanes = { voiced.chords, voiced.bass, voiced.top }; break;
    }

    so::midi::MidiFileSpec spec;
    spec.bpm = prog.bpm;
    spec.numerator = processor.getTransport().numerator.load();
    spec.denominator = processor.getTransport().denominator.load();

    const auto bytes = so::midi::writeSmf (lanes, spec);
    return file.replaceWithData (bytes.data(), bytes.size());
}

juce::String NativeDragStrip::fileNameFor (const juce::String& suggestionTitle, Lane lane)
{
    auto title = sanitise (suggestionTitle);
    if (title.isEmpty())
        title = "Suggestion";
    return "SecondOpinion_Session_" + title + "_" + sanitise (laneLabel (lane)) + ".mid";
}

void NativeDragStrip::exportToFolder()
{
    if (processor.progression.chords.empty())
        return;

    chooser = std::make_unique<juce::FileChooser> ("Export MIDI to a folder",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    juce::Component::SafePointer<NativeDragStrip> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;

        const auto dir = fc.getResult();
        if (! dir.isDirectory())
            return;

        for (auto lane : { Lane::Chords, Lane::Bass, Lane::Top, Lane::All })
            safe->writeLane (lane, dir.getChildFile (fileNameFor (safe->processor.progression.title, lane)));
    });
}

void NativeDragStrip::cleanUpOldDrags()
{
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::hours (1.0);
    for (const auto& entry : juce::RangedDirectoryIterator (dragRoot(), false, "*", juce::File::findDirectories))
        if (entry.getModificationTime() < cutoff)
            entry.getFile().deleteRecursively();
}
