#include "PluginEditor.h"
#include "SkinLayout.h"

#if THECALM_HAS_ARTWORK
 #include "BinaryData.h"
#endif

namespace
{
namespace Palette
{
const juce::Colour desk { 0xffe8ebe8 };       // around the pedal
const juce::Colour deskShade { 0xffdde1de };
const juce::Colour enclosure { 0xfffbfbf8 };
const juce::Colour enclosureEdge { 0xffe3e6e2 };
const juce::Colour ink { 0xff34414c };        // text and pointers
const juce::Colour inkSoft { 0xff8a97a1 };
const juce::Colour sky { 0xff8cc3e6 };        // accent of the built-in (vector) look
const juce::Colour skyDeep { 0xff5f9cc4 };
const juce::Colour ledOff { 0xffd5dde2 };
const juce::Colour skinInk { 0xff2b2f2a };    // dark ink of the skin's lettering
const juce::Colour skinSage { 0xff8aa07c };   // the sage of the Chill knob
const juce::Colour skinCream { 0xfff6efe2 };
} // namespace Palette

void softShadow (juce::Graphics& g, const juce::Path& shape, float radius, float alpha, float yOffset)
{
    juce::DropShadow (juce::Colours::black.withAlpha (alpha), juce::jmax (1, juce::roundToInt (radius)),
                      { 0, juce::roundToInt (yOffset) })
        .drawForPath (g, shape);
}

juce::Path ellipsePath (juce::Rectangle<float> r)
{
    juce::Path p;
    p.addEllipse (r);
    return p;
}

} // namespace

namespace CalmUI
{
juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions ("Avenir Next", height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const char* paramId, const juce::String& captionText, bool isLarge)
    : state (s), param (s.getParameter (paramId)), caption (captionText), large (isLarge),
      attachment (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, dial))
{
    dial.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    dial.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    dial.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    dial.setMouseDragSensitivity (220);
    dial.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    dial.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible (dial);
}

void Knob::bindTo (const char* paramId)
{
    attachment.reset();
    param = state.getParameter (paramId);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, dial);
    dial.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    changed();
}

void Knob::setSweep (float halfSweepDegrees)
{
    dial.setRotaryParameters (juce::degreesToRadians (360.0f - halfSweepDegrees), juce::degreesToRadians (360.0f + halfSweepDegrees), true);
    dial.repaint();
}

void Knob::setSkinned (float inner, float outer, float width)
{
    skinned = true;
    pointerIn = inner;
    pointerOut = outer;
    pointerWidth = width;
    resized();
    repaint();
}

void Knob::resized()
{
    auto b = getLocalBounds();
    if (skinned)
    {
        dial.setBounds (b); // the knob's bounds are the square around its top face
        return;
    }
    b.removeFromBottom (juce::roundToInt ((float) getHeight() * (large ? 0.2f : 0.3f)));
    dial.setBounds (b);
}

void Knob::paint (juce::Graphics& g)
{
    const bool showValue = dial.isMouseOverOrDragging() || dial.isMouseButtonDown();
    if (skinned)
        return; // the caption is on the artwork, the value bubble is drawn by the editor
    auto text = getLocalBounds().removeFromBottom (juce::roundToInt ((float) getHeight() * (large ? 0.2f : 0.3f))).toFloat();
    const float h = text.getHeight() * (large ? 0.72f : 0.62f);
    g.setFont (font (h, ! showValue));
    g.setColour (showValue ? Palette::skyDeep : Palette::ink);
    g.drawText (showValue ? param->getCurrentValueAsText() : caption, text, juce::Justification::centredTop);
}

void Knob::setLearning (bool shouldLearn)
{
    if (learning != shouldLearn)
    {
        learning = shouldLearn;
        dial.repaint();
    }
}

void Footswitch::setLearning (bool shouldLearn)
{
    if (learning != shouldLearn)
    {
        learning = shouldLearn;
        repaint();
    }
}

void PillSwitch::refresh()
{
    if (const bool on = param.getValue() > 0.5f; on != shownOn)
    {
        shownOn = on;
        repaint();
    }
}

void PillSwitch::setLearning (bool shouldLearn)
{
    if (learning != shouldLearn)
    {
        learning = shouldLearn;
        repaint();
    }
}

// Red ring that marks a control waiting for a MIDI CC.
static void paintLearningRing (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float t = juce::jmax (1.5f, r.getWidth() * 0.035f);
    g.setColour (juce::Colour (0xffff1f3d).withAlpha (0.35f));
    g.drawEllipse (r.reduced (t * 0.5f), t * 3.0f);
    g.setColour (juce::Colour (0xffff1f3d));
    g.drawEllipse (r.reduced (t * 0.5f), t);
}

void Knob::changed()
{
    repaint();
    if (skinned)
        if (auto* editor = dynamic_cast<TheCalmEditor*> (getParentComponent()))
            editor->repaint (editor->valueBubbleArea (*this));
}

void Knob::paintSkinned (juce::Graphics& g, juce::Rectangle<float> bounds, float angle) const
{
    // The knob itself is part of the artwork (its painted pointer was erased by prepare_skin.py);
    // only the pointer is drawn, turning about the centre of the knob's top face.
    const float R = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    juce::Path pointer;
    pointer.startNewSubPath (c.getPointOnCircumference (R * pointerIn, angle));
    pointer.lineTo (c.getPointOnCircumference (R * pointerOut, angle));
    const float width = juce::jmax (1.2f, R * pointerWidth);
    // An engraved groove lit from above: the lower lip catches the light, the groove is dark,
    // and its upper inner wall is in shadow. The light stays put while the knob turns.
    auto stroke = [&] (juce::Colour colour, float strokeWidth, juce::Point<float> offset) {
        auto p = pointer;
        p.applyTransform (juce::AffineTransform::translation (offset));
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    stroke (juce::Colours::white.withAlpha (0.45f), width * 1.45f, { 0.0f, width * 0.35f });
    stroke (juce::Colour (0xff2a2622), width, {});
    stroke (juce::Colours::black.withAlpha (0.35f), width * 0.5f, { 0.0f, -width * 0.2f });
}

void Knob::Dial::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
    const auto c = bounds.getCentre();
    const auto rp = getRotaryParameters();
    const float pos = (float) valueToProportionOfLength (getValue());
    const float angle = rp.startAngleRadians + pos * (rp.endAngleRadians - rp.startAngleRadians);
    const bool big = owner.large;

    if (owner.skinned)
    {
        owner.paintSkinned (g, bounds, angle);
        if (owner.learning)
            paintLearningRing (g, bounds);
        return;
    }

    // Dotted scale; dots up to the current value glow sky blue.
    const int dots = big ? 11 : 9;
    for (int i = 0; i < dots; ++i)
    {
        const float t = (float) i / (float) (dots - 1);
        const float a = rp.startAngleRadians + t * (rp.endAngleRadians - rp.startAngleRadians);
        const auto p = c.getPointOnCircumference (radius * 0.93f, a);
        const float d = radius * (big ? 0.07f : 0.09f);
        g.setColour (t <= pos + 1.0e-4f ? Palette::sky : Palette::ledOff);
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (p));
    }

    // Cap: soft shadow, matte white body, slight rim.
    const float capR = radius * 0.72f;
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    softShadow (g, ellipsePath (cap), capR * 0.22f, 0.22f, capR * 0.1f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, cap.getX(), cap.getY(),
                                             juce::Colour (0xffe6e9e7), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (Palette::enclosureEdge.darker (0.05f));
    g.drawEllipse (cap.reduced (0.5f), juce::jmax (1.0f, capR * 0.025f));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawEllipse (cap.reduced (capR * 0.14f), juce::jmax (0.8f, capR * 0.02f));

    // Pointer: a short rounded stroke.
    juce::Path pointer;
    const auto p1 = c.getPointOnCircumference (capR * 0.35f, angle);
    const auto p2 = c.getPointOnCircumference (capR * 0.86f, angle);
    pointer.startNewSubPath (p1);
    pointer.lineTo (p2);
    g.setColour (Palette::ink);
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.5f, capR * 0.09f), juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    if (owner.learning)
        paintLearningRing (g, bounds);
}

void Knob::Dial::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && owner.buildMenu != nullptr)
    {
        juce::PopupMenu menu;
        owner.buildMenu (menu);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<Knob> (&owner)] (int result) {
                                if (safe != nullptr && result != 0 && safe->menuResult)
                                    safe->menuResult (result);
                            });
        return;
    }
    Slider::mouseDown (e);
}

//==============================================================================
Footswitch::Footswitch (juce::AudioProcessorValueTreeState& s, const char* paramId, const char* latchId, const juce::String& captionText)
    : param (*s.getParameter (paramId)), latch (*s.getParameter (latchId)), caption (captionText)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Footswitch::set (bool on)
{
    // Only one octave at a time: switching one on releases the other.
    if (on && partner != nullptr && partner->isOn())
        partner->set (false);
    param.beginChangeGesture();
    param.setValueNotifyingHost (on ? 1.0f : 0.0f);
    param.endChangeGesture();
    repaint();
}

void Footswitch::setLatching (bool shouldLatch)
{
    latch.setValueNotifyingHost (shouldLatch ? 1.0f : 0.0f);
    set (false);
}

void Footswitch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Latch", true, isLatching());
        if (extendMenu)
        {
            menu.addSeparator();
            extendMenu (menu);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<Footswitch> (this)] (int result) {
                                if (safe == nullptr || result == 0)
                                    return;
                                if (result != 1)
                                {
                                    if (safe->extraResult)
                                        safe->extraResult (result);
                                    return;
                                }
                                safe->setLatching (! safe->isLatching());
                            });
        return;
    }
    set (isLatching() ? ! isOn() : true);
}

void Footswitch::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && ! isLatching())
        set (false);
}

bool Footswitch::hitTest (int x, int y)
{
    if (! skinned)
        return true;
    return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.44f;
}

void Footswitch::paint (juce::Graphics& g)
{
    if (skinned)
    {
        const auto b = getLocalBounds().toFloat();
        const float r = b.getWidth() * 0.5f;
        if (isOn() && skinImage.isValid())
        {
            // Pressed: the opening in the collar stays put; the whole cap (top and bevel) slides
            // along the switch axis. Where it moved away from, the collar's inner wall shows.
            const float k = b.getWidth() / skinSource.getWidth();
            const auto toLocal = juce::AffineTransform::translation (-skinSource.getX(), -skinSource.getY()).scaled (k);
            // Mask: symmetric about the switch's vertical axis; its sides come from the measured circle,
            // its top and bottom from the cap's highlight and bevel edges.
            auto ellipse = [this, &toLocal] (juce::Point<float> centre, float scale) {
                juce::ignoreUnused (scale);
                const juce::Point<float> offset = centre - holeCentre;
                const float cx = axisX + offset.x, cy = holeCentre.y + offset.y;
                const float rx = holeRadii.x + (axisX - holeCentre.x);
                const float top = maskTop + offset.y, bottom = maskBottom + offset.y;
                juce::Path p;
                for (int i = 0; i <= 180; ++i)
                {
                    const float a = juce::MathConstants<float>::twoPi * (float) i / 180.0f; // 0 = right, clockwise in screen y
                    const float sy = std::sin (a);
                    const float ca = std::cos (a);
                    // Lower-left quarter can reach a little further (fades out towards 6 and 9 o'clock).
                    const float extra = (sy > 0.0f && ca < 0.0f) ? maskBottomLeftExtra * std::sin (2.0f * (a - juce::MathConstants<float>::halfPi)) : 0.0f;
                    const juce::Point<float> q (cx + (rx + extra * 0.7f) * ca, cy + sy * (sy < 0.0f ? cy - top : bottom - cy + extra));
                    if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
                }
                p.closeSubPath();
                p.applyTransform (toLocal);
                return p;
            };

            const float depth = 1.0f; // a plain slide: no shrinking
            const auto movedCentre = holeCentre + pressShift;
            const auto hole = ellipse (holeCentre, 1.0f);
            const auto cap = ellipse (movedCentre, depth);
            const auto holeLocal = holeCentre.transformedBy (toLocal);
            const auto shiftLocal = pressShift * k;

            // Pressed: the cap is shaded slightly and slides straight up inside its mask; the strip
            // it leaves at the bottom of the mask is the dark gap between cap and collar.
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1a19), holeLocal + juce::Point<float> (0.0f, holeRadii.y * k),
                                                     juce::Colour (0xff3a3835), holeLocal, false));
            g.fillPath (hole);
            {
                juce::Graphics::ScopedSaveState clipToCap (g);
                g.reduceClipRegion (cap);
                const auto artToLocal = juce::AffineTransform::translation (-holeCentre.x, -holeCentre.y)
                                            .scaled (depth)
                                            .translated (movedCentre)
                                            .followedBy (toLocal);
                g.drawImageTransformed (skinImage, artToLocal);
                g.setColour (juce::Colours::black.withAlpha (0.12f));
                g.fillPath (cap);
            }
            juce::ignoreUnused (shiftLocal);
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.strokePath (cap, juce::PathStrokeType (juce::jmax (1.0f, 0.6f * k)));
        }
        if (learning)
            paintLearningRing (g, b.reduced (r * 0.02f));
        return;
    }

    auto b = getLocalBounds().toFloat();
    const auto label = b.removeFromBottom (b.getHeight() * 0.24f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const bool down = isOn();

    // Hex nut
    juce::Path nut;
    nut.addPolygon (c, 6, r * 0.98f, juce::MathConstants<float>::pi / 6.0f);
    g.setColour (juce::Colour (0xffe2e5e2));
    g.fillPath (nut);
    g.setColour (juce::Colour (0xffc9cfcc));
    g.strokePath (nut, juce::PathStrokeType (juce::jmax (1.0f, r * 0.03f)));

    // Dome
    const float domeR = r * (down ? 0.6f : 0.64f);
    const auto dome = juce::Rectangle<float> (domeR * 2.0f, domeR * 2.0f).withCentre (c.translated (0.0f, down ? r * 0.03f : 0.0f));
    softShadow (g, ellipsePath (dome), r * (down ? 0.06f : 0.16f), down ? 0.15f : 0.25f, r * (down ? 0.02f : 0.08f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, dome.getX(), dome.getY(),
                                             juce::Colour (down ? 0xffd2d9dd : 0xffe9ecea), dome.getRight(), dome.getBottom(), false));
    g.fillEllipse (dome);
    g.setColour (juce::Colour (0xffc4cbc8));
    g.drawEllipse (dome, juce::jmax (1.0f, r * 0.025f));
    if (learning)
        paintLearningRing (g, juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

    g.setColour (Palette::ink);
    g.setFont (font (label.getHeight() * 0.62f, true));
    g.drawText (caption, label, juce::Justification::centred);
    if (isLatching())
    {
        g.setColour (Palette::skyDeep);
        g.setFont (font (label.getHeight() * 0.36f));
        g.drawText ("latch", label.translated (0.0f, label.getHeight() * 0.55f), juce::Justification::centred);
    }
}

//==============================================================================
PresetBar::PresetBar (PresetManager& p) : presets (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
}

bool PresetBar::overPrev (juce::Point<float> p) const
{
    return p.getDistanceFrom (prevCentre) <= buttonRadius;
}

bool PresetBar::overNext (juce::Point<float> p) const
{
    return p.getDistanceFrom (nextCentre) <= buttonRadius;
}

void PresetBar::refresh()
{
    const auto name = presets.currentName();
    const bool modified = presets.isModified();
    if (name != shownName || modified != shownModified)
    {
        shownName = name;
        shownModified = modified;
        repaint();
    }
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto mouse = getMouseXYRelative().toFloat();
    const bool hover = isMouseOver();

    // The arrow buttons are printed on the artwork; darken the one under the mouse a touch.
    for (auto [centre, over] : { std::pair { prevCentre, overPrev (mouse) }, std::pair { nextCentre, overNext (mouse) } })
        if (hover && over)
        {
            g.setColour (juce::Colours::black.withAlpha (0.08f));
            g.fillEllipse (juce::Rectangle<float> (buttonRadius * 2.0f, buttonRadius * 2.0f).withCentre (centre));
        }

    const auto text = shownName + (shownModified ? " *" : "");
    if (SkinLayout::presetWindowIsDark)
    {
        // A little display: warm light lettering, centred.
        const auto area = window.reduced (window.getWidth() * 0.05f, window.getHeight() * 0.1f);
        g.setFont (juce::Font (juce::FontOptions ("Avenir Next", area.getHeight() * 0.62f, juce::Font::bold)));
        g.setColour (juce::Colour (0xfff3e6c8).withAlpha (hover && window.contains (mouse) ? 1.0f : 0.92f));
        g.drawFittedText (text, area.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        return;
    }

    // Preset name over a picture: white lettering with a soft dark halo, low in the window.
    const auto band = window.withTrimmedTop (window.getHeight() * 0.38f).reduced (window.getWidth() * 0.05f, window.getHeight() * 0.04f);
    g.setFont (juce::Font (juce::FontOptions ("Avenir Next", band.getHeight() * 0.8f, juce::Font::bold)));
    const float halo = juce::jmax (1.0f, band.getHeight() * 0.04f);
    g.setColour (juce::Colours::black.withAlpha (hover && window.contains (mouse) ? 0.5f : 0.35f));
    for (auto offset : { juce::Point<float> (halo, halo), juce::Point<float> (-halo, halo), juce::Point<float> (0.0f, halo * 1.6f) })
        g.drawFittedText (text, band.translated (offset.x, offset.y).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    g.setColour (juce::Colours::white);
    g.drawFittedText (text, band.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (overPrev (p))
        presets.step (-1);
    else if (overNext (p))
        presets.step (1);
    else if (window.contains (p))
        showMenu();
    refresh();
}

void PresetBar::showMenu()
{
    juce::PopupMenu factory, user, menu;
    const auto current = presets.currentName();
    const auto& factoryPresets = PresetManager::factoryPresets();
    for (size_t i = 0; i < factoryPresets.size(); ++i)
        factory.addItem (1 + (int) i, factoryPresets[i].name, true, current == factoryPresets[i].name);

    const auto userNames = presets.userPresetNames();
    for (int i = 0; i < userNames.size(); ++i)
        user.addItem (1000 + i, userNames[i], true, current == userNames[i]);

    menu.addSectionHeader ("Factory");
    for (juce::PopupMenu::MenuItemIterator it (factory); it.next();)
        menu.addItem (it.getItem());
    menu.addSectionHeader ("User");
    if (userNames.isEmpty())
        menu.addItem (-1, "(none yet)", false);
    for (juce::PopupMenu::MenuItemIterator it (user); it.next();)
        menu.addItem (it.getItem());
    menu.addSeparator();
    menu.addItem (2000, "Save Preset...");
    menu.addItem (2001, "Delete \"" + current + "\"", presets.isUserPreset (current));
    menu.addItem (2002, "Show Presets Folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<PresetBar> (this), userNames] (int result) {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& pm = safe->presets;
                            if (result >= 1 && result < 1000)
                                pm.loadFactory (result - 1);
                            else if (result >= 1000 && result < 2000)
                                pm.loadUser (userNames[result - 1000]);
                            else if (result == 2000)
                                safe->askToSave();
                            else if (result == 2001)
                            {
                                pm.deleteUser (pm.currentName());
                                pm.loadFactory (0);
                            }
                            else if (result == 2002)
                            {
                                PresetManager::userFolder().createDirectory();
                                PresetManager::userFolder().revealToUser();
                            }
                            safe->refresh();
                        });
}

void PresetBar::askToSave()
{
    auto* dialog = new juce::AlertWindow ("Save Preset", "Name your preset:", juce::MessageBoxIconType::NoIcon, this);
    const auto current = presets.currentName();
    dialog->addTextEditor ("name", presets.isUserPreset (current) ? current : juce::String());
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create (
                                       [safe = juce::Component::SafePointer<PresetBar> (this), dialog] (int result) {
                                           if (safe != nullptr && result == 1)
                                           {
                                               safe->presets.saveUser (dialog->getTextEditorContents ("name"));
                                               safe->refresh();
                                           }
                                       }),
                             true);
}

//==============================================================================
PillSwitch::PillSwitch (juce::AudioProcessorValueTreeState& s, const char* paramId) : param (*s.getParameter (paramId))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PillSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && buildMenu)
    {
        juce::PopupMenu menu;
        buildMenu (menu);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<PillSwitch> (this)] (int result) {
                                if (safe != nullptr && result != 0 && safe->menuResult)
                                    safe->menuResult (result);
                            });
        return;
    }
    param.beginChangeGesture();
    param.setValueNotifyingHost (param.getValue() > 0.5f ? 0.0f : 1.0f);
    param.endChangeGesture();
    repaint();
}

void PillSwitch::paint (juce::Graphics& g)
{
    const bool stereoOn = param.getValue() > 0.5f;
    if (skinned)
    {
        const auto b = getLocalBounds().toFloat();
        if (! stereoOn && skinImage.isValid())
        {
            // Mono: mirror the toggle about the housing's own axis - the housing maps onto itself and
            // only the lever changes sides.
            const float k = b.getWidth() / skinSource.getWidth();
            const auto toLocal = juce::AffineTransform::translation (-skinSource.getX(), -skinSource.getY()).scaled (k);
            g.drawImageTransformed (skinImage, juce::AffineTransform::scale (-1.0f, 1.0f, SkinLayout::toggleMirrorX, 0.0f).followedBy (toLocal));
        }
        if (learning)
        {
            g.setColour (juce::Colour (0xffff1f3d));
            g.drawRoundedRectangle (b.reduced (1.0f), b.getHeight() * 0.5f, juce::jmax (1.5f, b.getHeight() * 0.08f));
        }
        return;
    }
    auto b = getLocalBounds().toFloat();
    const float h = b.getHeight();
    auto left = b.removeFromLeft (h);
    auto right = b.removeFromRight (h);
    auto pill = b.reduced (h * 0.12f, h * 0.22f);

    g.setColour (Palette::inkSoft);
    g.drawEllipse (left.reduced (h * 0.3f), juce::jmax (1.0f, h * 0.06f));
    auto r2 = right.reduced (h * 0.3f);
    g.drawEllipse (r2.translated (-h * 0.09f, 0.0f), juce::jmax (1.0f, h * 0.06f));
    g.drawEllipse (r2.translated (h * 0.09f, 0.0f), juce::jmax (1.0f, h * 0.06f));

    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (Palette::enclosureEdge.darker (0.1f));
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);
    const float knobD = pill.getHeight() * 0.8f;
    const float x = stereoOn ? pill.getRight() - knobD - pill.getHeight() * 0.1f : pill.getX() + pill.getHeight() * 0.1f;
    g.setColour (stereoOn ? Palette::sky : Palette::inkSoft);
    g.fillEllipse (x, pill.getCentreY() - knobD * 0.5f, knobD, knobD);
}
} // namespace CalmUI

//==============================================================================
TheCalmEditor::TheCalmEditor (TheCalmProcessor& p)
    : AudioProcessorEditor (p), calmProcessor (p),
      input (p.state, ParamIDs::input, "INPUT", false),
      gate (p.state, ParamIDs::gate, "GATE", false),
      blend (p.state, ParamIDs::blend, "BLEND", false),
      output (p.state, ParamIDs::output, "OUTPUT", false),
      chill (p.state, ParamIDs::panic, "CHILL", true),
      order (p.state, ParamIDs::chaos, "ORDER", true),
      sloth (p.state, ParamIDs::speed, "SLOTH", true),
      rise (p.state, ParamIDs::rise, "RISE", false),
      stereo (p.state, ParamIDs::stereo),
      up (p.state, ParamIDs::octave1, ParamIDs::latch1, "UP"),
      wayUp (p.state, ParamIDs::octave2, ParamIDs::latch2, "WAY UP"),
      presetBar (p.presets)
{
    // Read before anything resizes the editor (resized() records the width).
    const int savedWidth = juce::jlimit (0, baseWidth * 2, calmProcessor.getSavedEditorWidth());

#if THECALM_HAS_ARTWORK
    skin = juce::ImageCache::getFromMemory (BinaryData::skin_png, BinaryData::skin_pngSize);
#endif

    for (auto* c : std::initializer_list<juce::Component*> { &input, &gate, &blend, &output, &chill, &order,
                                                             &sloth, &rise, &stereo, &up, &wayUp })
        addAndMakeVisible (c);
    // The preset window only exists on the skin artwork.
    addChildComponent (presetBar);

    if (skin.isValid())
    {
        auto skinKnob = [] (CalmUI::Knob& knob, const SkinLayout::Knob& spot) {
            knob.setSkinned (spot.pointerIn / spot.radius, spot.pointerOut / spot.radius, spot.pointerWidth / spot.radius);
        };
        skinKnob (input, SkinLayout::input);
        skinKnob (gate, SkinLayout::gate);
        skinKnob (blend, SkinLayout::blend);
        skinKnob (output, SkinLayout::output);
        skinKnob (chill, SkinLayout::chill);
        skinKnob (order, SkinLayout::order);
        skinKnob (sloth, SkinLayout::sloth);
        skinKnob (rise, SkinLayout::rise);
        up.skinned = wayUp.skinned = stereo.skinned = true;
        stereo.skinImage = up.skinImage = wayUp.skinImage = skin;
        presetBar.setVisible (true);
    }

    // Right-click menus: MIDI Learn everywhere, plus Tempo Sync on Rise.
    for (auto* knob : { &input, &gate, &blend, &output, &chill, &order, &sloth, &rise })
    {
        knob->buildMenu = [this, knob] (juce::PopupMenu& menu) {
            if (knob == &rise)
            {
                auto& s = calmProcessor.state;
                const bool sync = s.getRawParameterValue (ParamIDs::riseSync)->load() > 0.5f;
                const int div = (int) s.getRawParameterValue (ParamIDs::riseDiv)->load();
                menu.addItem (1, "Tempo Sync", true, sync);
                juce::PopupMenu divs;
                const auto& names = TheCalmProcessor::riseDivisionNames();
                for (int i = 0; i < names.size(); ++i)
                    divs.addItem (100 + i, names[i], true, i == div);
                menu.addSubMenu ("Sync Division", divs, sync);
                menu.addSeparator();
            }
            addMidiItems (menu, knob->paramID());
        };
        knob->menuResult = [this, knob] (int result) {
            if (handleMidiItem (result, knob->paramID()))
                return;
            auto& s = calmProcessor.state;
            if (result == 1)
            {
                auto* sync = s.getParameter (ParamIDs::riseSync);
                sync->setValueNotifyingHost (sync->getValue() > 0.5f ? 0.0f : 1.0f);
            }
            else if (result >= 100)
            {
                auto* div = s.getParameter (ParamIDs::riseDiv);
                div->setValueNotifyingHost (div->convertTo0to1 ((float) (result - 100)));
            }
        };
    }
    for (auto* fs : { &up, &wayUp })
    {
        fs->extendMenu = [this, fs] (juce::PopupMenu& menu) { addMidiItems (menu, fs->paramID()); };
        fs->extraResult = [this, fs] (int result) { handleMidiItem (result, fs->paramID()); };
    }
    up.partner = &wayUp;
    wayUp.partner = &up;
    stereo.buildMenu = [this] (juce::PopupMenu& menu) { addMidiItems (menu, stereo.paramID()); };
    stereo.menuResult = [this] (int result) { handleMidiItem (result, stereo.paramID()); };

    setResizable (true, true);
    setResizeLimits (baseWidth * 2 / 3, baseHeight * 2 / 3, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    ledOn = calmProcessor.engaged.load (std::memory_order_relaxed);
    setSize (savedWidth > 0 ? savedWidth : 360, savedWidth > 0 ? savedWidth * baseHeight / baseWidth : 540);
    restoringSize = false;
    riseShowsDivision = calmProcessor.state.getRawParameterValue (ParamIDs::riseSync)->load() > 0.5f;
    if (riseShowsDivision)
        rise.bindTo (ParamIDs::riseDiv);
    startTimerHz (60);
}

juce::Rectangle<float> TheCalmEditor::pedalBounds() const
{
    const float s = scale();
    return { 30.0f * s, 128.0f * s, 420.0f * s, 570.0f * s };
}

void TheCalmEditor::timerCallback()
{
    const bool on = calmProcessor.engaged.load (std::memory_order_relaxed);
    if (on != ledOn)
    {
        ledOn = on;
        repaint();
    }
    up.repaint();
    wayUp.repaint();
    presetBar.refresh();
    stereo.refresh();
    // With Tempo Sync on, the Rise knob shows and sets the division that is actually in use.
    if (const bool sync = calmProcessor.state.getRawParameterValue (ParamIDs::riseSync)->load() > 0.5f; sync != riseShowsDivision)
    {
        riseShowsDivision = sync;
        rise.bindTo (sync ? ParamIDs::riseDiv : ParamIDs::rise);
    }
    updateLearningHighlights();

    // Meters: instant attack, 60 dB/s release.
    if (skin.isValid())
    {
        bool moved = false;
        for (int i = 0; i < 4; ++i)
        {
            const float peak = calmProcessor.takePeak (i);
            const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
            const float next = juce::jmax (db, meterDb[i] - 60.0f / 60.0f);
            moved = moved || std::abs (next - meterDb[i]) > 0.05f;
            meterDb[i] = next;
        }
        if (moved)
        {
            const float k = (float) getWidth() / skinWidth;
            for (const auto& column : { SkinLayout::meterLeft, SkinLayout::meterRight })
                repaint (juce::Rectangle<float>::leftTopRightBottom (column.x0 - 12.0f, column.y0 - 16.0f, column.x1 + 12.0f, column.y1 + 16.0f)
                             .transformedBy (juce::AffineTransform::scale (k)).toNearestInt());
        }
    }
}

void TheCalmEditor::addMidiItems (juce::PopupMenu& menu, const juce::String& paramId)
{
    auto& learn = calmProcessor.midiLearn;
    menu.addItem (5000, learn.armedParameter() == paramId ? "Stop MIDI Learn" : "MIDI Learn");
    if (const auto bound = learn.describe (paramId); bound.isNotEmpty())
        menu.addItem (5001, "Clear MIDI (" + bound + ")");
}

bool TheCalmEditor::handleMidiItem (int result, const juce::String& paramId)
{
    auto& learn = calmProcessor.midiLearn;
    if (result == 5000)
    {
        if (learn.armedParameter() == paramId)
            learn.disarm();
        else
            learn.arm (paramId);
    }
    else if (result == 5001)
    {
        learn.clear (paramId);
    }
    else
    {
        return false;
    }
    updateLearningHighlights();
    return true;
}

void TheCalmEditor::updateLearningHighlights()
{
    const auto armed = calmProcessor.midiLearn.armedParameter();
    for (auto* knob : { &input, &gate, &blend, &output, &chill, &order, &sloth, &rise })
        knob->setLearning (knob->paramID() == armed);
    up.setLearning (up.paramID() == armed);
    wayUp.setLearning (wayUp.paramID() == armed);
    stereo.setLearning (stereo.paramID() == armed);
}

juce::Rectangle<int> TheCalmEditor::valueBubbleArea (const CalmUI::Knob& knob) const
{
    const float k = (float) getWidth() / skinWidth;
    const auto b = knob.getBounds().toFloat();
    return juce::Rectangle<float> (340.0f * k, 76.0f * k).withCentre ({ b.getCentreX(), b.getBottom() + 42.0f * k }).toNearestInt();
}

void TheCalmEditor::paintOverChildren (juce::Graphics& g)
{
    if (! skin.isValid())
        return;
    const float k = (float) getWidth() / skinWidth;
    for (auto* knob : { &input, &gate, &blend, &output, &chill, &order, &sloth, &rise })
    {
        if (! knob->isShowingValue())
            continue;
        const auto area = valueBubbleArea (*knob).toFloat();
        const auto text = knob->valueText();
        const float h = 58.0f * k;
        const auto f = CalmUI::font (38.0f * k, true);
        const float w = juce::GlyphArrangement::getStringWidth (f, text) + h * 0.9f;
        const auto pill = juce::Rectangle<float> (juce::jmin (w, area.getWidth()), h).withCentre (area.getCentre());
        juce::Path shape;
        shape.addRoundedRectangle (pill, h * 0.5f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.3f), juce::roundToInt (8.0f * k), { 0, juce::roundToInt (2.0f * k) }).drawForPath (g, shape);
        g.setColour (Palette::skinCream);
        g.fillPath (shape);
        g.setColour (Palette::skinSage);
        g.strokePath (shape, juce::PathStrokeType (2.5f * k));
        g.setColour (Palette::skinInk);
        g.setFont (f);
        g.drawText (text, pill, juce::Justification::centred);
    }
}

void TheCalmEditor::paintMeters (juce::Graphics& g)
{
    // The lamp columns in the top corners are painted fully lit: input on the left, output on the
    // right (louder of L/R). Lamps above the level are switched off: dark glass with a hint of colour.
    // 11 lamps from -30 to 0 dB.
    const float k = (float) getWidth() / skinWidth;
    constexpr int lamps = SkinLayout::meterLamps;
    constexpr float floorDb = -30.0f;
    const float levels[2] = { juce::jmax (meterDb[0], meterDb[1]), juce::jmax (meterDb[2], meterDb[3]) };
    const SkinLayout::Rect columns[2] = { SkinLayout::meterLeft, SkinLayout::meterRight };
    for (int m = 0; m < 2; ++m)
    {
        const int lit = juce::jlimit (0, lamps, (int) std::ceil ((levels[m] - floorDb) / -floorDb * (float) lamps));
        const auto& col = columns[m];
        const float cx = 0.5f * (col.x0 + col.x1);
        for (int i = lit; i < lamps; ++i)
        {
            const float y = SkinLayout::meterLampY[lamps - 1 - i];
            const auto lampColour = skin.getPixelAt (juce::roundToInt (cx), juce::roundToInt (y));
            // Cover the lit lamp and its glow with the housing, then a dim lamp.
            const auto cell = juce::Rectangle<float>::leftTopRightBottom (col.x0 - 3.0f, y - 11.0f, col.x1 + 3.0f, y + 11.0f)
                                  .transformedBy (juce::AffineTransform::scale (k));
            g.setColour (juce::Colour (0xff1c1b19));
            g.fillRoundedRectangle (cell, 3.0f * k);
            const auto lamp = juce::Rectangle<float>::leftTopRightBottom (col.x0, y - 8.0f, col.x1, y + 8.0f)
                                  .transformedBy (juce::AffineTransform::scale (k));
            g.setColour (lampColour.darker (2.2f).withMultipliedSaturation (0.7f));
            g.fillRoundedRectangle (lamp, 2.5f * k);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (lamp.withHeight (lamp.getHeight() * 0.4f).reduced (lamp.getWidth() * 0.12f, 0.0f), 2.0f * k);
        }
    }
}

void TheCalmEditor::paint (juce::Graphics& g)
{
    if (skin.isValid())
        paintSkin (g);
    else
        paintVector (g);
}

void TheCalmEditor::paintSkin (juce::Graphics& g)
{
    const float k = (float) getWidth() / skinWidth;
    g.drawImage (skin, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    paintMeters (g);

    // Status LED: painted lit; when the effect is off, cover the lens with dark amber glass.
    const juce::Point<float> c (SkinLayout::led.cx * k, SkinLayout::led.cy * k);
    const float lensR = SkinLayout::led.radius * 0.8f * k; // the glass; a thin orange rim stays visible
    const auto lens = juce::Rectangle<float> (lensR * 2.0f, lensR * 2.0f).withCentre (c);
    if (ledOn)
    {
        g.setColour (juce::Colour (0xffffc542).withAlpha (0.22f));
        g.fillEllipse (lens.expanded (lensR * 0.7f));
    }
    else
    {
        // Off: dark amber glass with a small highlight.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6e4a14), c.translated (-lensR * 0.3f, -lensR * 0.3f),
                                                 juce::Colour (0xff2e1d08), c.translated (lensR, lensR), true));
        g.fillEllipse (lens);
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.fillEllipse (juce::Rectangle<float> (lensR * 0.7f, lensR * 0.45f).withCentre (c.translated (-lensR * 0.25f, -lensR * 0.45f)));
    }
}

void TheCalmEditor::paintVector (juce::Graphics& g)
{
    using namespace CalmUI;
    const float s = scale();

    // Desk
    g.fillAll (Palette::desk);
    const float shelfBottom = 104.0f * s;

    // Cables plugged into the pedal's top jacks
    auto pedal = pedalBounds();
    for (float x : { 150.0f, 330.0f })
    {
        const float plugTop = pedal.getY() - 30.0f * s;
        juce::Path cable;
        cable.startNewSubPath (x * s, shelfBottom - 4.0f * s);
        cable.lineTo (x * s, plugTop);
        g.setColour (juce::Colour (0xffcdd4d1));
        g.strokePath (cable, juce::PathStrokeType (10.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xfff6f7f5));
        g.strokePath (cable, juce::PathStrokeType (6.5f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto plug = juce::Rectangle<float> (20.0f * s, 30.0f * s).withCentre ({ x * s, plugTop + 15.0f * s });
        juce::Path plugPath;
        plugPath.addRoundedRectangle (plug, 5.0f * s);
        softShadow (g, plugPath, 6.0f * s, 0.18f, 2.0f * s);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, plug.getX(), 0.0f, juce::Colour (0xffe2e6e3), plug.getRight(), 0.0f, false));
        g.fillPath (plugPath);
        g.setColour (juce::Colour (0xffc9cfcc));
        g.strokePath (plugPath, juce::PathStrokeType (1.0f * s));
        g.setColour (Palette::sky.withAlpha (0.8f));
        g.fillRect (plug.withHeight (2.5f * s).withY (plug.getY() + 8.0f * s).reduced (3.0f * s, 0.0f));
    }

    // Shelf with the utility knobs; the cables come out from under it
    const auto shelf = getLocalBounds().toFloat().withHeight (shelfBottom);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f8f6), 0.0f, 0.0f, juce::Colour (0xffeff1ef), 0.0f, shelfBottom, false));
    g.fillRect (shelf);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.08f), 0.0f, shelfBottom,
                                             juce::Colours::transparentBlack, 0.0f, shelfBottom + 8.0f * s, false));
    g.fillRect (shelf.withY (shelfBottom).withHeight (8.0f * s));
    g.setColour (Palette::enclosureEdge);
    g.drawHorizontalLine (juce::roundToInt (shelfBottom), 0.0f, (float) getWidth());

    // Branding
    g.setColour (Palette::ink);
    g.setFont (font (17.0f * s, true));
    g.drawText ("unknown", juce::Rectangle<float> (150.0f * s, 22.0f * s, 78.0f * s, 24.0f * s), juce::Justification::centredRight);
    g.setColour (Palette::skyDeep);
    g.setFont (font (15.0f * s));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")), juce::Rectangle<float> (228.0f * s, 22.0f * s, 24.0f * s, 24.0f * s), juce::Justification::centred);
    g.setColour (Palette::ink);
    g.setFont (font (17.0f * s, true));
    g.drawText ("betarabbit", juce::Rectangle<float> (252.0f * s, 22.0f * s, 90.0f * s, 24.0f * s), juce::Justification::centredLeft);

    // Enclosure
    juce::Path body;
    body.addRoundedRectangle (pedal, 28.0f * s);
    softShadow (g, body, 26.0f * s, 0.22f, 12.0f * s);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, pedal.getX(), pedal.getY(),
                                             juce::Colour (0xfff1f2ee), pedal.getRight(), pedal.getBottom(), false));
    g.fillPath (body);
    g.setColour (Palette::enclosureEdge);
    g.strokePath (body, juce::PathStrokeType (1.2f * s));

    // Fine print along the top edge
    g.setColour (Palette::inkSoft);
    g.setFont (font (10.0f * s, true));
    g.drawText ("9VDC  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x98\xbe")) + "  NO RUSH",
                juce::Rectangle<float> (pedal.getX(), pedal.getY() + 14.0f * s, pedal.getWidth(), 14.0f * s),
                juce::Justification::centred);

    // Lettering
    g.setColour (Palette::ink);
    g.setFont (font (64.0f * s, true).italicised());
    g.drawText ("the calm", juce::Rectangle<float> (pedal.getX(), 430.0f * s, pedal.getWidth(), 80.0f * s), juce::Justification::centred);
    g.setColour (Palette::sky);
    g.setFont (font (13.0f * s, true));
    g.drawText ("z  z  z", juce::Rectangle<float> (300.0f * s, 425.0f * s, 80.0f * s, 20.0f * s), juce::Justification::centred);

    // Status LED
    const auto led = juce::Rectangle<float> (10.0f * s, 10.0f * s).withCentre ({ pedal.getCentreX(), 425.0f * s });
    if (ledOn)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Palette::sky.withAlpha (0.12f * (float) (4 - i)));
            g.fillEllipse (led.expanded ((float) i * 3.5f * s));
        }
    }
    g.setColour (ledOn ? Palette::sky : Palette::ledOff);
    g.fillEllipse (led);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.drawEllipse (led, 0.8f * s);

    // Thin line between the footswitches
    g.setColour (Palette::sky.withAlpha (0.6f));
    g.drawLine (175.0f * s, 610.0f * s, 305.0f * s, 610.0f * s, 1.2f * s);
    g.fillEllipse (juce::Rectangle<float> (5.0f * s, 5.0f * s).withCentre ({ 240.0f * s, 610.0f * s }));
}

void TheCalmEditor::resized()
{
    if (skin.isValid())
    {
        // Positions come from SkinLayout.h, generated by tools/prepare_skin.py from the artwork.
        const float k = (float) getWidth() / skinWidth;
        auto toScreen = [k] (juce::Rectangle<float> r) { return r.transformedBy (juce::AffineTransform::scale (k)); };
        auto placeKnob = [&] (juce::Component& c, const SkinLayout::Knob& spot) {
            c.setBounds (toScreen (juce::Rectangle<float> (spot.radius * 2.0f, spot.radius * 2.0f).withCentre ({ spot.cx, spot.cy })).toNearestInt());
        };
        placeKnob (input, SkinLayout::input);
        placeKnob (gate, SkinLayout::gate);
        placeKnob (blend, SkinLayout::blend);
        placeKnob (output, SkinLayout::output);
        placeKnob (chill, SkinLayout::chill);
        placeKnob (order, SkinLayout::order);
        placeKnob (sloth, SkinLayout::sloth);
        placeKnob (rise, SkinLayout::rise);

        // Footswitches: the opening around cap and bevel; pressing slides it up, a touch towards the centre.
        // Both switches are drawn the same way (only the lighting differs), so they share one mask
        // size and one press: straight up.
        const float maskRadius = 0.5f * (SkinLayout::upSwitch.radius + SkinLayout::wayUpSwitch.radius);
        auto placeSwitch = [&] (CalmUI::Footswitch& fs, const SkinLayout::Circle& hole, float axis) {
            fs.setBounds (toScreen (juce::Rectangle<float> (maskRadius * 2.6f + 2.0f * (axis - hole.cx), maskRadius * 2.6f).withCentre ({ axis, hole.cy })).toNearestInt());
            fs.skinSource = fs.getBounds().toFloat() / k;
            fs.holeCentre = { hole.cx, hole.cy };
            fs.holeRadii = { maskRadius, maskRadius };
            fs.holeAngle = 0.0f;
            fs.pressShift = { 0.0f, -3.0f };
            fs.axisX = juce::jmax (axis, hole.cx); // never narrower than the measured circle
        };
        placeSwitch (up, SkinLayout::upSwitch, SkinLayout::upSwitchAxis);
        placeSwitch (wayUp, SkinLayout::wayUpSwitch, SkinLayout::wayUpSwitchAxis);
        // Top: just above the cap's highlight; bottom: just below its bevel (measured along each axis).
        up.maskTop = 1235.0f;
        up.maskBottomLeftExtra = 2.5f;
        up.maskBottom = 1351.5f;
        wayUp.maskTop = 1235.0f;
        wayUp.maskBottom = 1351.5f;

        const auto toggle = juce::Rectangle<float>::leftTopRightBottom (SkinLayout::toggle.x0, SkinLayout::toggle.y0,
                                                                       SkinLayout::toggle.x1, SkinLayout::toggle.y1)
;
        const float halfWidth = juce::jmax (SkinLayout::toggleMirrorX - toggle.getX(), toggle.getRight() - SkinLayout::toggleMirrorX);
        stereo.setBounds (toScreen (toggle.withLeft (SkinLayout::toggleMirrorX - halfWidth).withRight (SkinLayout::toggleMirrorX + halfWidth)).toNearestInt());
        stereo.skinSource = stereo.getBounds().toFloat() / k;

        // Preset bar: from the left arrow button to the right one, with the picture window between.
        const auto& pb = SkinLayout::prevButton;
        const auto& nb = SkinLayout::nextButton;
        const auto area = juce::Rectangle<float>::leftTopRightBottom (pb.cx - pb.radius, juce::jmin (pb.cy, nb.cy) - pb.radius,
                                                                      nb.cx + nb.radius, juce::jmax (pb.cy, nb.cy) + nb.radius);
        presetBar.setBounds (toScreen (area).toNearestInt());
        const auto origin = presetBar.getPosition().toFloat();
        presetBar.prevCentre = juce::Point<float> (pb.cx, pb.cy) * k - origin;
        presetBar.nextCentre = juce::Point<float> (nb.cx, nb.cy) * k - origin;
        presetBar.buttonRadius = 0.5f * (pb.radius + nb.radius) * k;
        presetBar.window = toScreen (juce::Rectangle<float>::leftTopRightBottom (SkinLayout::presetWindow.x0, SkinLayout::presetWindow.y0,
                                                                                SkinLayout::presetWindow.x1, SkinLayout::presetWindow.y1))
                               - origin;

        if (! restoringSize)
            calmProcessor.setSavedEditorWidth (getWidth());
        for (auto* fs : { &up, &wayUp })
            fs->skinImage = skin;
        return;
    }

    const float s = scale();
    auto place = [s] (juce::Component& c, float cx, float cy, float w, float h) {
        c.setBounds (juce::Rectangle<float> (w * s, h * s).withCentre ({ cx * s, cy * s }).toNearestInt());
    };

    place (input, 42.0f, 56.0f, 64.0f, 84.0f);
    place (gate, 104.0f, 56.0f, 64.0f, 84.0f);
    place (blend, 376.0f, 56.0f, 64.0f, 84.0f);
    place (output, 438.0f, 56.0f, 64.0f, 84.0f);
    place (stereo, 240.0f, 68.0f, 86.0f, 22.0f);

    place (order, 240.0f, 232.0f, 124.0f, 150.0f);
    place (chill, 118.0f, 318.0f, 124.0f, 150.0f);
    place (sloth, 362.0f, 318.0f, 124.0f, 150.0f);
    place (rise, 240.0f, 362.0f, 66.0f, 86.0f);

    place (up, 118.0f, 612.0f, 96.0f, 124.0f);
    place (wayUp, 362.0f, 612.0f, 96.0f, 124.0f);
}
