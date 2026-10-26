#include "PluginEditor.h"

#include "Theme.h"
#include "bridge/WebBridge.h"
#include "services/SessionEngine.h"

#include "SecondOpinionUIData.h"

namespace
{
    juce::ZipFile* uiArchive()
    {
        // Built once per process and shared by every instance (NFR-15). Read-only after creation.
        static juce::MemoryInputStream stream (SecondOpinionUIData::ui_zip, (size_t) SecondOpinionUIData::ui_zipSize, false);
        static juce::ZipFile zip (stream);
        return &zip;
    }

    const char* mimeFor (const juce::String& path)
    {
        const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (ext == "html" || ext == "htm") return "text/html";
        if (ext == "js" || ext == "mjs")   return "text/javascript";
        if (ext == "css")                  return "text/css";
        if (ext == "json" || ext == "map") return "application/json";
        if (ext == "svg")                  return "image/svg+xml";
        if (ext == "png")                  return "image/png";
        if (ext == "woff2")                return "font/woff2";
        if (ext == "woff")                 return "font/woff";
        if (ext == "ico")                  return "image/vnd.microsoft.icon";
        return "application/octet-stream";
    }
}

SecondOpinionEditor::SecondOpinionEditor (SecondOpinionProcessor& p)
    : AudioProcessorEditor (&p), processor (p), dragStrip (p)
{
    for (const auto* id : so::params::colorIds)
        colorRelays.push_back (std::make_unique<juce::WebToggleButtonRelay> (id));

    auto options = createWebOptions();

    if (juce::WebBrowserComponent::areOptionsSupported (options))
    {
        web = std::make_unique<Browser> (options);
        addAndMakeVisible (*web);

        // Engine events go straight to the page; cached results are replayed once it has loaded.
        auto& engine = processor.getEngine();
        engine.emit = [this] (const juce::String& name, const juce::var& payload)
        {
            if (web != nullptr)
                web->emitEventIfBrowserIsVisible (name, payload);
        };
        juce::Component::SafePointer<SecondOpinionEditor> safe (this);
        web->onLoaded = [safe]
        {
            // Give the page's own boot code a moment to subscribe to events.
            juce::Timer::callAfterDelay (300, [safe] { if (safe != nullptr) safe->processor.getEngine().replay(); });
        };
    }
    else
    {
       #if JUCE_WINDOWS
        // E.g. the WebView2 runtime is missing on an older Windows install.
        fallbackMessage.setText ("Second Opinion needs the Microsoft Edge WebView2 Runtime to show its interface. "
                                 "Install it from Microsoft, then reopen the plugin. Your project and settings are safe.",
                                 juce::dontSendNotification);
       #else
        fallbackMessage.setText ("Second Opinion couldn't start its interface on this system. "
                                 "Your project and settings are safe.",
                                 juce::dontSendNotification);
       #endif
        fallbackMessage.setJustificationType (juce::Justification::centred);
        fallbackMessage.setColour (juce::Label::textColourId, so::theme::text);
        addAndMakeVisible (fallbackMessage);
    }

    // Parameter ↔ UI bindings: host automation moves the UI and UI gestures write automation.
    auto& s = processor.apvts;
    auto* um = s.undoManager;
    auto bindSlider = [&] (const char* id, juce::WebSliderRelay& relay)
    {
        if (auto* param = s.getParameter (id))
            sliderAttachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (*param, relay, um));
    };

    bindSlider (so::params::tension, tensionRelay);
    bindSlider (so::params::smoothness, smoothnessRelay);
    bindSlider (so::params::density, densityRelay);
    bindSlider (so::params::rangeLow, rangeLowRelay);
    bindSlider (so::params::rangeHigh, rangeHighRelay);
    bindSlider (so::params::previewLevel, previewLevelRelay);

    if (auto* param = s.getParameter (so::params::bassMotion))
        comboAttachment = std::make_unique<juce::WebComboBoxParameterAttachment> (*param, bassMotionRelay, um);

    if (auto* param = s.getParameter (so::params::melodyOnTop))
        toggleAttachments.push_back (std::make_unique<juce::WebToggleButtonParameterAttachment> (*param, melodyOnTopRelay, um));

    for (size_t i = 0; i < colorRelays.size(); ++i)
        if (auto* param = s.getParameter (so::params::colorIds[i]))
            toggleAttachments.push_back (std::make_unique<juce::WebToggleButtonParameterAttachment> (*param, *colorRelays[i], um));

    addAndMakeVisible (dragStrip);
    processor.onProgressionChanged = [this] { dragStrip.refresh(); };

    if (web != nullptr)
        web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // Size: remember the last window size per project (FR-SET-04).
    constrainer.setSizeLimits (minWidth, minHeight, maxWidth, maxHeight);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const int w = juce::jlimit (minWidth, maxWidth, (int) processor.session.getProperty ("uiWidth", defaultWidth));
    const int h = juce::jlimit (minHeight, maxHeight, (int) processor.session.getProperty ("uiHeight", defaultHeight));
    setSize (w, h);

    startTimerHz (20);   // capture meter, audition playhead and transport
}

SecondOpinionEditor::~SecondOpinionEditor()
{
    processor.getEngine().emit = nullptr;
    processor.onProgressionChanged = nullptr;
}

juce::WebBrowserComponent::Options SecondOpinionEditor::createWebOptions()
{
    using Options = juce::WebBrowserComponent::Options;

    auto options = Options{}
       #if JUCE_WINDOWS
        .withBackend (Options::Backend::webview2)   // Edge WebView2 (only valid on Windows)
       #endif
        .withWinWebView2Options (Options::WinWebView2{}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                              .getChildFile ("SecondOpinion")
                                                              .getChildFile ("WebView2"))
                                     .withBackgroundColour (so::theme::bg)
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withOptionsFrom (tensionRelay)
        .withOptionsFrom (smoothnessRelay)
        .withOptionsFrom (densityRelay)
        .withOptionsFrom (rangeLowRelay)
        .withOptionsFrom (rangeHighRelay)
        .withOptionsFrom (previewLevelRelay)
        .withOptionsFrom (bassMotionRelay)
        .withOptionsFrom (melodyOnTopRelay);

    for (auto& relay : colorRelays)
        options = options.withOptionsFrom (*relay);

    juce::Component::SafePointer<SecondOpinionEditor> safe (this);
    return so::bridge::addNativeFunctions (std::move (options), processor, [safe]
    {
        if (safe != nullptr)
            safe->dragStrip.exportToFolder();
    });
}

std::optional<juce::WebBrowserComponent::Resource> SecondOpinionEditor::getResource (const juce::String& url)
{
    // Strip query/fragment and the leading slash; "/" serves index.html. No network, ever (FR-UX-01).
    auto path = url.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    path = path.trimCharactersAtStart ("/");
    if (path.isEmpty())
        path = "index.html";

    auto* zip = uiArchive();
    if (auto* entry = zip->getEntry (path))
    {
        std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (*entry));
        if (in == nullptr)
            return std::nullopt;

        std::vector<std::byte> data ((size_t) entry->uncompressedSize);
        in->read (data.data(), (int) data.size());
        return juce::WebBrowserComponent::Resource { std::move (data), mimeFor (path) };
    }

    return std::nullopt;
}

void SecondOpinionEditor::paint (juce::Graphics& g)
{
    g.fillAll (so::theme::bg);
}

void SecondOpinionEditor::resized()
{
    auto r = getLocalBounds();
    dragStrip.setBounds (r.removeFromBottom (dragStripHeight));

    if (web != nullptr)
        web->setBounds (r);
    else
        fallbackMessage.setBounds (r.reduced (48));

    processor.session.setProperty ("uiWidth", getWidth(), nullptr);
    processor.session.setProperty ("uiHeight", getHeight(), nullptr);
}

void SecondOpinionEditor::timerCallback()
{
    if (web == nullptr)
        return;

    processor.getEngine().tick();

    const auto& t = processor.getTransport();
    LastTransport now { t.playing.load(), t.bpm.load(), t.ppq.load(), t.numerator.load(), t.denominator.load(), t.hasTempo.load() };

    const bool changed = now.playing != lastSent.playing || now.bpm != lastSent.bpm || now.num != lastSent.num
                      || now.den != lastSent.den || now.hasTempo != lastSent.hasTempo
                      || (now.playing && now.ppq != lastSent.ppq);

    if (! changed)
        return;

    lastSent = now;

    auto* o = new juce::DynamicObject();
    o->setProperty ("playing", now.playing);
    o->setProperty ("bpm", now.bpm);
    o->setProperty ("ppq", now.ppq);
    o->setProperty ("numerator", now.num);
    o->setProperty ("denominator", now.den);
    o->setProperty ("hasTempo", now.hasTempo);
    web->emitEventIfBrowserIsVisible ("transport", juce::var (o));
}
