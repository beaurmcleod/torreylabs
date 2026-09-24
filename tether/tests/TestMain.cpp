#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <iostream>

namespace
{
    // JUCE's default runner logs through Logger, which on Windows goes to the
    // debugger instead of the console; print to stdout so CI logs show results.
    class ConsoleRunner final : public juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override
        {
            std::cout << message << std::endl;
        }
    };
}

int main (int argc, char** argv)
{
    // Processor and editor tests need JUCE's message manager.
    juce::ScopedJuceInitialiser_GUI juceInit;

    ConsoleRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);

    if (argc > 1)
    {
        // Optional filter: run only tests whose name contains argv[1].
        juce::Array<juce::UnitTest*> selected;
        for (auto* test : juce::UnitTest::getTestsInCategory ("Tether"))
            if (test->getName().containsIgnoreCase (argv[1]))
                selected.add (test);

        runner.runTests (selected);
    }
    else
    {
        runner.runTestsInCategory ("Tether");
    }

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult (i);
        failures += result->failures;
        passes += result->passes;
    }

    std::cout << "\n" << passes << " checks passed, " << failures << " failed\n";
    return failures > 0 ? 1 : 0;
}
