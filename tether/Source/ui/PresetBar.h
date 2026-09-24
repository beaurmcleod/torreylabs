#pragma once

#include "TetherLookAndFeel.h"
#include "../PresetManager.h"

namespace ui
{

/** Previous / name (opens the preset menu) / next / save. */
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (PresetManager& presets);
    ~PresetBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void showMenu();
    void askToSave();
    void refresh();

    PresetManager& presets;
    juce::TextButton previousButton { "<" }, nextButton { ">" }, nameButton, saveButton { "SAVE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace ui
