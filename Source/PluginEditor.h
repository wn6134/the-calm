#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Look and feel of The Calm: a white, soft, sleepy take on a stompbox.
namespace CalmUI
{
juce::Font font (float height, bool bold = false);

// Rotary knob drawn as a matte white cap with a dotted scale. Shows its value while touched.
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& caption, bool large);

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (juce::PopupMenu&)> buildMenu;
    std::function<void (int)> menuResult;

    // Skinned: the knob and its caption are part of the artwork; only the pointer is drawn,
    // from `inner` to `outer` and `width` wide, all as fractions of the knob's radius.
    void setSkinned (float inner, float outer, float width);
    // Rotation from the lowest to the highest value is +-halfSweepDegrees around straight up.
    void setSweep (float halfSweepDegrees);

    // Skinned knobs leave their value read-out to the editor, which draws it on top of everything.
    bool isShowingValue() const { return dial.isMouseOverOrDragging() || dial.isMouseButtonDown(); }
    juce::String valueText() const { return param->getCurrentValueAsText(); }

    juce::String paramID() const { return param->getParameterID(); }
    void setLearning (bool);
    // Points the knob at another parameter (Rise switches between milliseconds and a tempo division).
    void bindTo (const char* paramId);

private:
    class Dial final : public juce::Slider
    {
    public:
        explicit Dial (Knob& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent& e) override { Slider::mouseEnter (e); owner.changed(); }
        void mouseExit (const juce::MouseEvent& e) override { Slider::mouseExit (e); owner.changed(); }
        void valueChanged() override { owner.changed(); }
        void stoppedDragging() override { owner.changed(); }

    private:
        Knob& owner;
    };

    void paintSkinned (juce::Graphics&, juce::Rectangle<float> bounds, float angle) const;
    void changed();

    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter* param;
    juce::String caption;
    bool large;
    bool skinned = false, learning = false;
    float pointerIn = 0.2f, pointerOut = 0.9f, pointerWidth = 0.07f;
    Dial dial { *this };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Footswitch: momentary by default, latching when its latch parameter is on (right-click).
class Footswitch final : public juce::Component
{
public:
    Footswitch (juce::AudioProcessorValueTreeState&, const char* paramId, const char* latchId, const juce::String& caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

    // Skinned: the switch is printed on the artwork; only the pressed state is drawn.
    bool skinned = false;

    juce::String paramID() const { return param.getParameterID(); }
    void setLearning (bool);
    std::function<void (juce::PopupMenu&)> extendMenu; // extra right-click items (ids >= 100)
    std::function<void (int)> extraResult;

    // The other footswitch: only one can be on at a time.
    Footswitch* partner = nullptr;
    // Skinned: the artwork and where this switch sits on it, for the pressed-in look.
    // The collar opening is an ellipse in the artwork's pseudo-perspective. Pressing slides the
    // whole cap (top and bevel) deeper into it along the switch axis (pressShift).
    // All values are in artwork pixels.
    juce::Image skinImage;
    juce::Rectangle<float> skinSource;
    juce::Point<float> holeCentre, holeRadii, pressShift;
    float holeAngle = 0.0f; // radians
    float axisX = 0.0f;     // the mask is symmetric about this vertical line (artwork x)
    float maskTop = 0.0f, maskBottom = 0.0f; // artwork y of the moving part's top and bottom edges
    float maskBottomLeftExtra = 0.0f;        // extra reach of the lower-left quarter (6-9 o'clock)

private:
    void set (bool on);
    void setLatching (bool);
    bool learning = false;
    bool isOn() const { return param.getValue() > 0.5f; }
    bool isLatching() const { return latch.getValue() > 0.5f; }

    juce::RangedAudioParameter& param;
    juce::RangedAudioParameter& latch;
    juce::String caption;
};

// Preset window: arrows step through presets, the name opens a menu (load, save, delete).
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (PresetManager&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    // Called from the editor's timer; repaints when the name or modified flag changes.
    void refresh();

    // Skinned geometry in local coordinates: the two round arrow buttons and the picture window.
    juce::Point<float> prevCentre, nextCentre;
    float buttonRadius = 0.0f;
    juce::Rectangle<float> window;

private:
    bool overPrev (juce::Point<float>) const;
    bool overNext (juce::Point<float>) const;
    void showMenu();
    void askToSave();

    PresetManager& presets;
    juce::String shownName;
    bool shownModified = false;
};

// Little pill switch for mono / stereo.
class PillSwitch final : public juce::Component
{
public:
    PillSwitch (juce::AudioProcessorValueTreeState&, const char* paramId);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool skinned = false;
    // Skinned: the toggle is printed in the Stereo position; Mono shows it mirrored.
    juce::Image skinImage;
    juce::Rectangle<float> skinSource; // the toggle's housing on the artwork
    juce::String paramID() const { return param.getParameterID(); }
    void setLearning (bool);
    // Called from the editor's timer: follows changes made by presets, automation or MIDI.
    void refresh();
    std::function<void (juce::PopupMenu&)> buildMenu;
    std::function<void (int)> menuResult;

private:
    juce::RangedAudioParameter& param;
    bool learning = false;
    bool shownOn = false;
};
} // namespace CalmUI

class TheCalmEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TheCalmEditor (TheCalmProcessor&);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // For ui_snapshot: freeze the meters at given levels (input L/R, output L/R, dBFS).
    void setMeterLevelsForPreview (const float* db)
    {
        std::copy (db, db + 4, meterDb);
        stopTimer();
    }

    // Area where a skinned knob's value bubble is drawn (editor coordinates).
    juce::Rectangle<int> valueBubbleArea (const CalmUI::Knob&) const;

    static constexpr int baseWidth = 480, baseHeight = 720;
    // The skin artwork is drawn at 1024 x 1536; skinned layout uses its pixel coordinates.
    static constexpr float skinWidth = 1024.0f;

private:
    void timerCallback() override;
    float scale() const { return (float) getWidth() / (float) baseWidth; }
    juce::Rectangle<float> pedalBounds() const;
    void paintVector (juce::Graphics&);
    void paintSkin (juce::Graphics&);
    void paintMeters (juce::Graphics&);
    void addMidiItems (juce::PopupMenu&, const juce::String& paramId);
    bool handleMidiItem (int result, const juce::String& paramId);
    void updateLearningHighlights();

    TheCalmProcessor& calmProcessor;
    juce::Image skin;
    bool restoringSize = true; // don't remember sizes set while the editor is being built

    CalmUI::Knob input, gate, blend, output;
    CalmUI::Knob chill, order, sloth, rise;
    CalmUI::PillSwitch stereo;
    CalmUI::Footswitch up, wayUp;
    CalmUI::PresetBar presetBar;
    bool ledOn = false;
    bool riseShowsDivision = false; // Rise knob drives the tempo division while Tempo Sync is on
    float meterDb[4] { -100.0f, -100.0f, -100.0f, -100.0f }; // input L/R, output L/R, with fall-off

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheCalmEditor)
};
