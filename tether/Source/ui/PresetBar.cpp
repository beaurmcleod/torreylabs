#include "PresetBar.h"

namespace ui
{

PresetBar::PresetBar (PresetManager& p) : presets (p)
{
    for (auto* button : { &previousButton, &nextButton, &nameButton, &saveButton })
        addAndMakeVisible (button);

    previousButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    nameButton.setTooltip ("Choose a preset");
    saveButton.setTooltip ("Save the current settings as a user preset");
    nameButton.getProperties().set ("presetName", true);

    previousButton.onClick = [this] { presets.loadNext (-1); };
    nextButton.onClick = [this] { presets.loadNext (1); };
    nameButton.onClick = [this] { showMenu(); };
    saveButton.onClick = [this] { askToSave(); };

    presets.onPresetChanged = [this] { refresh(); };
    refresh();
}

PresetBar::~PresetBar()
{
    presets.onPresetChanged = nullptr;
}

void PresetBar::refresh()
{
    nameButton.setButtonText (presets.getCurrentName());
    repaint();
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    previousButton.setBounds (area.removeFromLeft (26));
    area.removeFromLeft (4);
    saveButton.setBounds (area.removeFromRight (48));
    area.removeFromRight (4);
    nextButton.setBounds (area.removeFromRight (26));
    area.removeFromRight (4);
    nameButton.setBounds (area);
}

void PresetBar::paint (juce::Graphics&)
{
}

void PresetBar::showMenu()
{
    juce::PopupMenu menu;
    juce::String category;

    for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
    {
        const auto& preset = presets.getFactoryPreset (i);

        if (category != preset.category)
        {
            category = preset.category;
            menu.addSectionHeader (category);
        }

        menu.addItem (1 + i, preset.name, true, presets.isCurrentFactory() && presets.getCurrentName() == preset.name);
    }

    const auto users = presets.getUserPresetNames();

    if (! users.isEmpty())
    {
        menu.addSectionHeader ("User");
        for (int i = 0; i < users.size(); ++i)
            menu.addItem (1000 + i, users[i], true, ! presets.isCurrentFactory() && presets.getCurrentName() == users[i]);
    }

    menu.addSeparator();
    menu.addItem (2000, "Save as...");
    menu.addItem (2001, "Show presets folder", presets.getUserDirectory().exists());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton).withMinimumWidth (220),
                        [this, users] (int result)
                        {
                            if (result == 0)
                                return;

                            if (result == 2000)
                                askToSave();
                            else if (result == 2001)
                                presets.getUserDirectory().revealToUser();
                            else if (result >= 1000)
                                presets.loadUserPreset (users[result - 1000]);
                            else
                                presets.loadFactoryPreset (result - 1);
                        });
}

void PresetBar::askToSave()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name for the preset:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", presets.isCurrentFactory() ? juce::String() : presets.getCurrentName());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result == 1)
            presets.saveUserPreset (window->getTextEditorContents ("name"));
    }), true);
}

} // namespace ui
