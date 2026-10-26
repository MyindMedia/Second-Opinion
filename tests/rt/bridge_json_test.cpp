#include <catch2/catch_test_macros.hpp>

#include "bridge/Json.h"

// Regression test: JUCE's Linux WebKit backend delivered "A♭maj7" as Latin-1-decoded UTF-8,
// so every chord with an accidental failed to parse in setProgression.
TEST_CASE ("Bridge repairs mis-decoded UTF-8 and leaves good strings alone", "[bridge]")
{
    using so::bridge::json::repairUtf8;
    const auto good = juce::String::fromUTF8 ("A\xe2\x99\xadmaj7/E\xe2\x99\xad");   // A♭maj7/E♭

    juce::String mangled;
    for (const char* p = "A\xe2\x99\xadmaj7/E\xe2\x99\xad"; *p != 0; ++p)
        mangled += juce::String::charToString ((juce::juce_wchar) (unsigned char) *p);

    CHECK (repairUtf8 (mangled) == good);
    CHECK (repairUtf8 (good) == good);
    CHECK (repairUtf8 ("Fm9") == "Fm9");
    CHECK (repairUtf8 (juce::String::charToString ((juce::juce_wchar) 0xe9)) == juce::String::charToString ((juce::juce_wchar) 0xe9));   // real Latin-1 é

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("symbol", mangled);
    const auto fixed = so::bridge::json::repair (juce::var (juce::Array<juce::var> { juce::var (obj) }));
    CHECK (fixed[0].getProperty ("symbol", {}).toString() == good);
}
