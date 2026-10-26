#include "AiMoodClient.h"

#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
#endif

namespace
{
    constexpr auto endpoint = "https://api.anthropic.com/v1/messages";
    constexpr int timeoutMs = 15000;

    /** Pulls the first {...} block out of the model's text, in case it adds prose around the JSON. */
    juce::String extractJsonObject (const juce::String& text)
    {
        const int start = text.indexOfChar ('{');
        const int end = text.lastIndexOfChar ('}');
        return (start >= 0 && end > start) ? text.substring (start, end + 1) : juce::String();
    }
}

AiMoodClient::AiMoodClient() = default;

AiMoodClient::~AiMoodClient()
{
    alive->store (false);
    pool.removeAllJobs (true, timeoutMs + 1000);
}

juce::File AiMoodClient::keyFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("SecondOpinion")
        .getChildFile ("ai.key");
}

bool AiMoodClient::hasKey()
{
    return keyFile().existsAsFile() && keyFile().loadFileAsString().trim().isNotEmpty();
}

void AiMoodClient::setKey (const juce::String& key)
{
    auto f = keyFile();
    if (key.trim().isEmpty())
    {
        f.deleteFile();
        return;
    }
    f.getParentDirectory().createDirectory();
    f.replaceWithText (key.trim());
   #if JUCE_MAC || JUCE_LINUX
    ::chmod (f.getFullPathName().toRawUTF8(), 0600);  // owner read/write only
   #endif
}

juce::String AiMoodClient::maskedKey()
{
    if (! hasKey())
        return {};
    const auto k = keyFile().loadFileAsString().trim();
    return k.substring (0, 7) + "..." + k.getLastCharacters (4);
}

void AiMoodClient::cancel()
{
    pool.removeAllJobs (true, 100);
}

void AiMoodClient::interpret (const juce::String& description, const juce::String& systemPrompt, Callback onDone)
{
    cancel();

    const auto key = hasKey() ? keyFile().loadFileAsString().trim() : juce::String();
    auto aliveFlag = alive;
    const auto modelId = model;

    pool.addJob ([key, description, systemPrompt, modelId, aliveFlag, onDone = std::move (onDone)]
    {
        Result r;

        if (key.isEmpty())
        {
            r.error = "Add an API key in Settings to use online AI.";
        }
        else
        {
            juce::DynamicObject::Ptr msg (new juce::DynamicObject());
            msg->setProperty ("role", "user");
            msg->setProperty ("content", description.substring (0, 2000)); // text only, bounded

            juce::DynamicObject::Ptr body (new juce::DynamicObject());
            body->setProperty ("model", modelId);
            body->setProperty ("max_tokens", 800);
            body->setProperty ("system", systemPrompt);
            body->setProperty ("messages", juce::Array<juce::var> { juce::var (msg.get()) });

            const auto payload = juce::JSON::toString (juce::var (body.get()), true);
            int status = 0;

            auto stream = juce::URL (endpoint)
                              .withPOSTData (payload)
                              .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                      .withExtraHeaders ("Content-Type: application/json\r\n"
                                                                         "x-api-key: " + key + "\r\n"
                                                                         "anthropic-version: 2023-06-01\r\n")
                                                      .withConnectionTimeoutMs (timeoutMs)
                                                      .withStatusCode (&status));

            if (stream == nullptr)
            {
                r.error = "Couldn't reach the AI service. Using the offline mood engine.";
            }
            else
            {
                const auto response = juce::JSON::parse (stream->readEntireStreamAsString());

                if (status != 200)
                {
                    const auto apiMessage = response.getProperty ("error", {}).getProperty ("message", {}).toString();
                    r.error = status == 401 ? juce::String ("The API key was rejected. Check it in Settings.")
                                            : "The AI service returned an error (" + juce::String (status) + "). "
                                                  + apiMessage.substring (0, 160);
                }
                else
                {
                    juce::String text;
                    if (const auto* content = response.getProperty ("content", {}).getArray())
                        for (const auto& block : *content)
                            if (block.getProperty ("type", {}).toString() == "text")
                                text << block.getProperty ("text", {}).toString();

                    r.json = extractJsonObject (text);
                    r.ok = r.json.isNotEmpty() && juce::JSON::parse (r.json).isObject();
                    if (! r.ok)
                        r.error = "The AI reply wasn't usable. Using the offline mood engine.";
                }
            }
        }

        juce::MessageManager::callAsync ([aliveFlag, onDone, r]
        {
            if (aliveFlag->load() && onDone)
                onDone (r);
        });
    });
}
