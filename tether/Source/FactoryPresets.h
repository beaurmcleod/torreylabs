#pragma once

#include "Parameters.h"

#include <vector>

/** A built-in preset: a name, the group it is listed under and its settings. */
struct FactoryPreset
{
    const char* name;
    const char* category;
    PluginSettings settings;
};

/** The built-in presets, in the order they are shown. The first one is "Init". */
const std::vector<FactoryPreset>& factoryPresets();
