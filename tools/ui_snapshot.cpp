// Renders The Calm's editor to a PNG, so the UI can be reviewed without a DAW.
//
//   ui_snapshot out.png [width] [paramId=value ...] [meters=inL,inR,outL,outR]
//
// Values are plain (unnormalised) parameter values, e.g. panic=0.7 octave1=1; meters in dBFS.
// preset=Name loads a preset, savepreset=Name saves the current settings (applied in order).
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: ui_snapshot out.png [width] [paramId=value ...]\n");
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    TheCalmProcessor processor;

    int width = 480;
    float meters[4] = { -100.0f, -100.0f, -100.0f, -100.0f };
    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg.startsWith ("preset="))
        {
            if (! processor.presets.load (arg.fromFirstOccurrenceOf ("=", false, false)))
                std::fprintf (stderr, "no such preset\n");
            continue;
        }
        if (arg.startsWith ("savepreset="))
        {
            if (! processor.presets.saveUser (arg.fromFirstOccurrenceOf ("=", false, false)))
                std::fprintf (stderr, "could not save preset\n");
            continue;
        }
        if (arg.startsWith ("meters="))
        {
            auto values = juce::StringArray::fromTokens (arg.fromFirstOccurrenceOf ("=", false, false), ",", "");
            for (int m = 0; m < juce::jmin (4, values.size()); ++m)
                meters[m] = values[m].getFloatValue();
            continue;
        }
        if (! arg.containsChar ('='))
        {
            width = arg.getIntValue();
            continue;
        }
        if (auto* p = processor.state.getParameter (arg.upToFirstOccurrenceOf ("=", false, false)))
            p->setValueNotifyingHost (p->convertTo0to1 (arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }
    processor.engaged = processor.state.getRawParameterValue (ParamIDs::octave1)->load() > 0.5f
                        || processor.state.getRawParameterValue (ParamIDs::octave2)->load() > 0.5f;

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (width, juce::roundToInt (width * 720.0 / 480.0));
    if (auto* calm = dynamic_cast<TheCalmEditor*> (editor.get()))
        calm->setMeterLevelsForPreview (meters);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! juce::PNGImageFormat().writeImageToStream (image, out))
        return 1;
    return 0;
}
