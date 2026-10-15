#include "MidiLearn.h"
#include "PluginProcessor.h"

MidiLearn::MidiLearn (juce::AudioProcessorValueTreeState& s) : state (s)
{
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            params.add (ranged);
    for (auto& slot : slotToParam)
        slot.store (-1);
}

int MidiLearn::indexOf (const juce::String& paramId) const
{
    for (int i = 0; i < params.size(); ++i)
        if (params[i]->getParameterID() == paramId)
            return i;
    return -1;
}

int MidiLearn::slotFor (int paramIndex) const
{
    for (int slot = 0; slot < (int) slotToParam.size(); ++slot)
        if (paramIndex >= 0 && slotToParam[(size_t) slot].load() == paramIndex)
            return slot;
    return -1;
}

void MidiLearn::arm (const juce::String& paramId)
{
    armed.store (indexOf (paramId));
}

juce::String MidiLearn::armedParameter() const
{
    const int index = armed.load();
    return juce::isPositiveAndBelow (index, params.size()) ? params[index]->getParameterID() : juce::String();
}

int MidiLearn::ccFor (const juce::String& paramId) const
{
    const int slot = slotFor (indexOf (paramId));
    return slot >= 0 && slot < noteOffset ? slot : -1;
}

int MidiLearn::noteFor (const juce::String& paramId) const
{
    const int slot = slotFor (indexOf (paramId));
    return slot >= noteOffset ? slot - noteOffset : -1;
}

juce::String MidiLearn::describe (const juce::String& paramId) const
{
    if (const int cc = ccFor (paramId); cc >= 0)
        return "CC " + juce::String (cc);
    if (const int note = noteFor (paramId); note >= 0)
        return "Note " + juce::MidiMessage::getMidiNoteName (note, true, true, 3);
    return {};
}

void MidiLearn::clear (const juce::String& paramId)
{
    const int index = indexOf (paramId);
    for (auto& slot : slotToParam)
        if (index >= 0 && slot.load() == index)
            slot.store (-1);
}

void MidiLearn::bind (int slot, int paramIndex)
{
    // One binding per parameter and one parameter per CC / note.
    for (auto& s : slotToParam)
        if (s.load() == paramIndex)
            s.store (-1);
    slotToParam[(size_t) slot].store (paramIndex);
}

void MidiLearn::process (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        int slot = -1, value = 0;
        bool pressed = false;
        if (msg.isController())
        {
            slot = msg.getControllerNumber();
            value = msg.getControllerValue();
            pressed = value >= 64;
        }
        else if (msg.isNoteOn())
        {
            slot = noteOffset + msg.getNoteNumber();
            value = msg.getVelocity();
            pressed = true;
        }
        else if (msg.isNoteOff())
        {
            slot = noteOffset + msg.getNoteNumber();
            pressed = false;
        }
        else
        {
            continue;
        }

        // Learning binds the next CC or note-on (a stray note-off is ignored).
        if (const int target = armed.load(); target >= 0 && (slot < noteOffset || pressed))
        {
            armed.store (-1);
            bind (slot, target);
            wasPressed[(size_t) slot] = pressed;
            continue;
        }

        if (const int index = slotToParam[(size_t) slot].load(); index >= 0)
            apply (index, slot, value, pressed);
        wasPressed[(size_t) slot] = pressed;
    }
}

void MidiLearn::apply (int paramIndex, int slot, int value, bool pressed)
{
    auto* p = params[paramIndex];
    const auto id = p->getParameterID();
    const bool isNote = slot >= noteOffset;
    float normalised = (float) value / 127.0f;

    const bool isSwitch1 = id == ParamIDs::octave1, isSwitch2 = id == ParamIDs::octave2;
    if (isSwitch1 || isSwitch2)
    {
        // Footswitches: pressed engages; in latch mode every new press toggles.
        const bool latching = state.getRawParameterValue (isSwitch1 ? ParamIDs::latch1 : ParamIDs::latch2)->load() > 0.5f;
        if (latching)
        {
            if (! pressed || wasPressed[(size_t) slot])
                return;
            normalised = p->getValue() > 0.5f ? 0.0f : 1.0f;
        }
        else
        {
            normalised = pressed ? 1.0f : 0.0f;
        }
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
    {
        if (isNote)
        {
            if (! pressed)
                return;
            normalised = p->getValue() > 0.5f ? 0.0f : 1.0f; // a key toggles
        }
        else
        {
            normalised = pressed ? 1.0f : 0.0f;
        }
    }
    else if (isNote && ! pressed)
    {
        return; // knobs follow note-on velocity only
    }

    // Only one octave at a time: switching one on releases the other.
    if ((isSwitch1 || isSwitch2) && normalised > 0.5f)
        if (auto* other = state.getParameter (isSwitch1 ? ParamIDs::octave2 : ParamIDs::octave1); other->getValue() > 0.5f)
            other->setValueNotifyingHost (0.0f);
    p->setValueNotifyingHost (normalised);
}

void MidiLearn::writeTo (juce::ValueTree& tree) const
{
    auto map = tree.getOrCreateChildWithName ("MidiMap", nullptr);
    map.removeAllChildren (nullptr);
    for (int slot = 0; slot < (int) slotToParam.size(); ++slot)
        if (const int index = slotToParam[(size_t) slot].load(); index >= 0)
        {
            juce::ValueTree entry (slot < noteOffset ? "CC" : "Note");
            entry.setProperty ("number", slot < noteOffset ? slot : slot - noteOffset, nullptr);
            entry.setProperty ("param", params[index]->getParameterID(), nullptr);
            map.appendChild (entry, nullptr);
        }
}

void MidiLearn::readFrom (const juce::ValueTree& tree)
{
    for (auto& slot : slotToParam)
        slot.store (-1);
    for (const auto& entry : tree.getChildWithName ("MidiMap"))
    {
        const int number = entry.getProperty ("number", -1);
        const int index = indexOf (entry.getProperty ("param").toString());
        if (juce::isPositiveAndBelow (number, 128) && index >= 0)
            slotToParam[(size_t) (entry.hasType ("Note") ? noteOffset + number : number)].store (index);
    }
}
