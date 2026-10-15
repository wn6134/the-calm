// Small self-checks for The Calm's non-DSP logic: MIDI Learn, presets, parameter defaults.
//   calm_tests   -> prints PASS/FAIL per check, exit code 1 on any failure.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

float plain (TheCalmProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }

void sendMidi (TheCalmProcessor& p, const juce::MidiMessage& m)
{
    juce::AudioBuffer<float> audio (2, 64);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent (m, 0);
    p.processBlock (audio, midi);
}

void sendCC (TheCalmProcessor& p, int cc, int value) { sendMidi (p, juce::MidiMessage::controllerEvent (1, cc, value)); }
void noteOn (TheCalmProcessor& p, int note) { sendMidi (p, juce::MidiMessage::noteOn (1, note, (juce::uint8) 100)); }
void noteOff (TheCalmProcessor& p, int note) { sendMidi (p, juce::MidiMessage::noteOff (1, note)); }
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    TheCalmProcessor p;
    p.prepareToPlay (48000.0, 64);

    // Defaults
    check (plain (p, ParamIDs::panic) == 0.5f && plain (p, ParamIDs::chaos) == 0.5f && plain (p, ParamIDs::speed) == 0.5f,
           "Chill/Order/Sloth default to noon");
    check (p.state.getParameter (ParamIDs::gate)->getCurrentValueAsText() == "Off", "Gate reads Off at its minimum");
    check (plain (p, ParamIDs::stereo) > 0.5f, "stereo by default");

    // MIDI Learn: knob
    p.midiLearn.arm (ParamIDs::panic);
    sendCC (p, 20, 5);
    check (p.midiLearn.ccFor (ParamIDs::panic) == 20, "CC 20 learned for Chill");
    check (p.midiLearn.armedParameter().isEmpty(), "learning disarms after one CC");
    sendCC (p, 20, 127);
    check (std::abs (plain (p, ParamIDs::panic) - 1.0f) < 1e-4f, "CC 20 = 127 turns Chill fully up");
    sendCC (p, 20, 0);
    check (plain (p, ParamIDs::panic) < 1e-4f, "CC 20 = 0 turns Chill fully down");

    // MIDI Learn: momentary footswitch
    p.midiLearn.arm (ParamIDs::octave1);
    sendCC (p, 64, 0);
    sendCC (p, 64, 127);
    check (plain (p, ParamIDs::octave1) > 0.5f, "momentary UP: pedal down engages");
    sendCC (p, 64, 0);
    check (plain (p, ParamIDs::octave1) < 0.5f, "momentary UP: pedal up releases");

    // MIDI Learn: latching footswitch toggles on each press
    p.state.getParameter (ParamIDs::latch2)->setValueNotifyingHost (1.0f);
    p.midiLearn.arm (ParamIDs::octave2);
    sendCC (p, 65, 0);
    sendCC (p, 65, 127);
    sendCC (p, 65, 0);
    check (plain (p, ParamIDs::octave2) > 0.5f, "latch WAY UP: first press latches on");
    sendCC (p, 65, 127);
    sendCC (p, 65, 0);
    check (plain (p, ParamIDs::octave2) < 0.5f, "latch WAY UP: second press latches off");

    // Only one octave at a time; Latch is per switch (WAY UP latching, UP momentary on CC 64)
    sendCC (p, 65, 127); // WAY UP latched on
    sendCC (p, 65, 0);
    check (plain (p, ParamIDs::octave2) > 0.5f, "WAY UP latched on again");
    sendCC (p, 64, 127); // UP held: momentary, and it releases WAY UP
    check (plain (p, ParamIDs::octave1) > 0.5f && plain (p, ParamIDs::octave2) < 0.5f,
           "holding UP releases latched WAY UP");
    sendCC (p, 64, 0);
    check (plain (p, ParamIDs::octave1) < 0.5f, "UP stays momentary: its own Latch is off");
    p.state.getParameter (ParamIDs::latch2)->setValueNotifyingHost (0.0f);

    // Keys (notes) work too: learn WAY UP on a key, then hold it
    p.midiLearn.arm (ParamIDs::octave2);
    noteOn (p, 60);
    noteOff (p, 60);
    check (p.midiLearn.noteFor (ParamIDs::octave2) == 60 && p.midiLearn.describe (ParamIDs::octave2) == "Note C3",
           "note C3 learned for WAY UP");
    noteOn (p, 60);
    check (plain (p, ParamIDs::octave2) > 0.5f, "holding the key engages WAY UP");
    noteOff (p, 60);
    check (plain (p, ParamIDs::octave2) < 0.5f, "releasing the key releases WAY UP");
    p.state.getParameter (ParamIDs::latch2)->setValueNotifyingHost (1.0f);
    noteOn (p, 60);
    noteOff (p, 60);
    check (plain (p, ParamIDs::octave2) > 0.5f, "latched WAY UP: a key press toggles on");
    noteOn (p, 60);
    noteOff (p, 60);
    check (plain (p, ParamIDs::octave2) < 0.5f, "latched WAY UP: next key press toggles off");
    p.state.getParameter (ParamIDs::latch2)->setValueNotifyingHost (0.0f);

    // One CC per parameter: relearning Chill moves it
    p.midiLearn.arm (ParamIDs::panic);
    sendCC (p, 21, 64);
    check (p.midiLearn.ccFor (ParamIDs::panic) == 21, "relearn moves Chill to CC 21");

    // MIDI map survives save/restore
    juce::MemoryBlock blob;
    p.getStateInformation (blob);
    TheCalmProcessor q;
    q.setStateInformation (blob.getData(), (int) blob.getSize());
    check (q.midiLearn.ccFor (ParamIDs::panic) == 21 && q.midiLearn.ccFor (ParamIDs::octave1) == 64
               && q.midiLearn.noteFor (ParamIDs::octave2) == 60,
           "MIDI map (CCs and notes) restored with the project");

    // Presets
    check (p.presets.load ("Sloth Mode") && std::abs (plain (p, ParamIDs::speed) - 1.0f) < 1e-4f, "factory preset loads");
    check (! p.presets.isModified(), "freshly loaded preset is not modified");
    p.state.getParameter (ParamIDs::panic)->setValueNotifyingHost (0.9f);
    check (p.presets.isModified(), "moving a knob marks the preset modified");
    p.state.getParameter (ParamIDs::stereo)->setValueNotifyingHost (1.0f);
    p.presets.load ("Overthinking");
    check (plain (p, ParamIDs::stereo) > 0.5f, "mono/stereo is left alone by presets");
    check (! p.presets.isModified(), "mono/stereo does not mark a preset modified");
    p.setCurrentProgram (1);
    check (p.presets.currentName() == "Clean Up" && p.getCurrentProgram() == 1, "host programs map to factory presets");

    // Editor size is remembered with the project
    {
        TheCalmProcessor r;
        r.setSavedEditorWidth (600);
        std::unique_ptr<juce::AudioProcessorEditor> editor (r.createEditor());
        check (editor->getWidth() == 600 && editor->getHeight() == 900, "editor reopens at the saved size");
        editor->setSize (520, 780);
        check (r.getSavedEditorWidth() == 520, "resizing records the new width");
    }
    {
        TheCalmProcessor synced;
        synced.presets.load ("On The Beat");
        std::unique_ptr<juce::AudioProcessorEditor> editor (synced.createEditor());
        bool riseShowsDivision = false;
        for (auto* c : editor->getChildren())
            if (auto* knob = dynamic_cast<CalmUI::Knob*> (c))
                riseShowsDivision = riseShowsDivision || knob->paramID() == ParamIDs::riseDiv;
        check (riseShowsDivision, "with Tempo Sync on, the Rise knob drives the division");
    }
    {
        TheCalmProcessor fresh;
        std::unique_ptr<juce::AudioProcessorEditor> editor (fresh.createEditor());
        check (editor->getWidth() == 360 && editor->getHeight() == 540, "first open uses 360 x 540");
    }

    std::printf ("%s\n", failures == 0 ? "ALL PASSED" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
