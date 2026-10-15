#include "PresetManager.h"
#include "PluginProcessor.h"

namespace
{
const juce::Identifier presetProperty ("preset");
const juce::Identifier presetRoot ("CalmPreset");
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : state (s)
{
}

const std::vector<PresetManager::Factory>& PresetManager::factoryPresets()
{
    using namespace ParamIDs;
    static const std::vector<Factory> presets {
        { "Default", {} },
        { "Clean Up", { { panic, 0.0f }, { chaos, 0.0f }, { speed, 0.0f } } },
        { "Nap Time", { { panic, 0.2f }, { chaos, 0.1f }, { speed, 0.3f }, { rise, 400.0f } } },
        { "Slow Morning", { { panic, 0.0f }, { chaos, 0.0f }, { speed, 0.0f }, { rise, 900.0f } } },
        { "Sloth Mode", { { panic, 0.0f }, { chaos, 0.3f }, { speed, 1.0f }, { rise, 150.0f } } },
        { "Overthinking", { { panic, 1.0f }, { chaos, 0.4f }, { speed, 0.0f } } },
        { "Half Asleep", { { panic, 0.3f }, { chaos, 0.2f }, { speed, 0.4f }, { blend, 0.5f } } },
        { "On The Beat", { { panic, 0.0f }, { chaos, 0.0f }, { speed, 0.0f }, { riseSync, 1.0f }, { riseDiv, 3.0f } } },
        { "Total Calm", { { panic, 1.0f }, { chaos, 1.0f }, { speed, 1.0f } } },
    };
    return presets;
}

juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("The Calm")
        .getChildFile ("Presets");
}

bool PresetManager::storesParameter (const juce::String& id) const
{
    // Footswitches, their latch modes and mono/stereo are performance/routing settings, not sound.
    return id != ParamIDs::octave1 && id != ParamIDs::octave2 && id != ParamIDs::latch1 && id != ParamIDs::latch2
           && id != ParamIDs::stereo;
}

juce::StringArray PresetManager::userPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : userFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::StringArray PresetManager::allNames() const
{
    juce::StringArray names;
    for (const auto& p : factoryPresets())
        names.add (p.name);
    names.addArray (userPresetNames());
    return names;
}

std::optional<PresetManager::Values> PresetManager::presetValues (const juce::String& name) const
{
    for (const auto& p : factoryPresets())
        if (name == p.name)
        {
            Values values;
            for (const auto& [id, value] : p.values)
                values[id] = value;
            return values;
        }

    const auto xml = juce::XmlDocument::parse (userFolder().getChildFile (name + extension));
    if (xml == nullptr || ! xml->hasTagName (presetRoot))
        return std::nullopt;
    Values values;
    for (auto* e : xml->getChildWithTagNameIterator ("Param"))
        values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
    return values;
}

void PresetManager::apply (const Values& values)
{
    for (auto* p : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (ranged == nullptr || ! storesParameter (ranged->getParameterID()))
            continue;
        const auto it = values.find (ranged->getParameterID());
        const float normalised = it != values.end() ? ranged->convertTo0to1 (it->second) : ranged->getDefaultValue();
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (normalised);
        ranged->endChangeGesture();
    }
}

void PresetManager::remember (const juce::String& name)
{
    state.state.setProperty (presetProperty, name, nullptr);
    snapshotName = {}; // rebuilt on the next isModified()
}

bool PresetManager::loadFactory (int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) && load (presets[(size_t) index].name);
}

bool PresetManager::loadUser (const juce::String& name)
{
    return isUserPreset (name) && load (name);
}

bool PresetManager::load (const juce::String& name)
{
    const auto values = presetValues (name);
    if (! values)
        return false;
    apply (*values);
    remember (name);
    return true;
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;
    juce::XmlElement xml (presetRoot);
    xml.setAttribute ("name", name);
    for (auto* p : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (ranged == nullptr || ! storesParameter (ranged->getParameterID()))
            continue;
        auto* e = xml.createNewChildElement ("Param");
        e->setAttribute ("id", ranged->getParameterID());
        e->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
    }
    const auto folder = userFolder();
    if (! folder.createDirectory() || ! xml.writeTo (folder.getChildFile (name + extension)))
        return false;
    remember (name);
    return true;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    if (! isUserPreset (name))
        return false;
    // Moves the file to the Trash rather than deleting it outright.
    return userFolder().getChildFile (name + extension).moveToTrash();
}

void PresetManager::step (int delta)
{
    const auto names = allNames();
    if (names.isEmpty())
        return;
    const int current = names.indexOf (currentName());
    const int next = ((current < 0 ? 0 : current + delta) % names.size() + names.size()) % names.size();
    load (names[next]);
}

juce::String PresetManager::currentName() const
{
    return state.state.getProperty (presetProperty, factoryPresets().front().name).toString();
}

bool PresetManager::isUserPreset (const juce::String& name) const
{
    return userFolder().getChildFile (name + extension).existsAsFile();
}

bool PresetManager::isModified() const
{
    const auto name = currentName();
    if (name != snapshotName)
    {
        snapshot.clear();
        snapshotName = name;
        const auto values = presetValues (name).value_or (Values {});
        for (auto* p : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && storesParameter (ranged->getParameterID()))
            {
                const auto it = values.find (ranged->getParameterID());
                snapshot[ranged->getParameterID()] = it != values.end() ? ranged->convertTo0to1 (it->second) : ranged->getDefaultValue();
            }
    }
    for (const auto& [id, value] : snapshot)
        if (auto* p = state.getParameter (id); p != nullptr && std::abs (p->getValue() - value) > 1.0e-4f)
            return true;
    return false;
}
