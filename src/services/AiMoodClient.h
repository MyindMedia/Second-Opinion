#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>

/** Opt-in online interpreter for song descriptions (ADR 0010).

    Off by default. When the user turns it on and stores an API key, the description TEXT (never
    audio, never chords) is sent to the Claude Messages API with a system prompt that asks for a
    MoodProfile JSON object (schema owned by src/core/song). Any failure falls back to the offline
    mood engine, so the plugin keeps working with no network.

    The key is stored in the user's application-data folder, not in the DAW project, so sharing a
    project never shares the key. All work runs on a background thread; the callback is delivered on
    the message thread. */
class AiMoodClient
{
public:
    struct Result
    {
        bool ok { false };
        juce::String json;      // model output (expected: MoodProfile JSON)
        juce::String error;     // human-readable reason when !ok
    };

    using Callback = std::function<void (const Result&)>;

    AiMoodClient();
    ~AiMoodClient();

    static bool hasKey();
    static void setKey (const juce::String& key);   // empty clears it
    static juce::String maskedKey();                 // "sk-ant-...abcd" for display

    /** Model id is configurable; default is a fast, low-cost model. */
    void setModel (const juce::String& modelId) { model = modelId; }

    /** Sends `description` with `systemPrompt`. Cancels any request still in flight. */
    void interpret (const juce::String& description, const juce::String& systemPrompt, Callback onDone);

    void cancel();

private:
    static juce::File keyFile();

    juce::String model { "claude-haiku-5-5" };
    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiMoodClient)
};
