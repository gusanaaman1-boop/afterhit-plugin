#include "Theme.h"

namespace ah::ui
{
    juce::Font font (float height, bool bold, float tracking)
    {
        auto o = juce::FontOptions().withHeight (height).withStyle (bold ? "Bold" : "Regular");
        if (tracking != 0.0f)
            o = o.withKerningFactor (tracking);
        return juce::Font (o);
    }

    juce::Font numberFont (float height)
    {
        return juce::Font (juce::FontOptions().withHeight (height).withFeatureEnabled ("tnum"));
    }

    void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                      juce::Font f, juce::Justification j)
    {
        g.setFont (f);
        g.drawText (text, area, j, false);
    }

    void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> b)
    {
        //  Heights as fractions of the box, left to right.
        constexpr float heights[] = { 0.62f, 1.0f, 0.74f, 0.9f, 0.52f };
        const float barW = b.getWidth() * 0.12f;
        const float step = (b.getWidth() - barW) / 4.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float h = b.getHeight() * heights[i];
            juce::Rectangle<float> r (b.getX() + step * (float) i, b.getCentreY() - h * 0.5f, barW, h);
            g.setColour (i < 2 ? colour::dry : colour::wet);
            g.fillRoundedRectangle (r, barW * 0.5f);
        }
    }

    void drawNaamanMark (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        auto box = b.withSizeKeepingCentre (d, d);
        const float s = d / 40.0f;
        auto at = [&box, s] (float x, float y) { return juce::Point<float> (box.getX() + x * s, box.getY() + y * s); };
        const juce::Colour bone (0xffeae7e0), brass (0xffc9a86a);

        g.setColour (bone.withAlpha (0.28f));
        g.drawEllipse (box.reduced (1.75f * s), 1.0f * s);
        g.setColour (bone);
        g.drawLine ({ at (13.2f, 27.4f), at (13.2f, 12.6f) }, 1.5f * s);
        g.drawLine ({ at (26.8f, 27.4f), at (26.8f, 12.6f) }, 1.5f * s);
        g.setColour (brass);
        g.drawLine ({ at (13.2f, 12.6f), at (26.8f, 27.4f) }, 1.5f * s);
    }

    Look::Look()
    {
        setColour (juce::ComboBox::textColourId, colour::text);
        setColour (juce::ComboBox::backgroundColourId, colour::lifted);
        setColour (juce::ComboBox::outlineColourId, colour::hairline);
        setColour (juce::ComboBox::arrowColourId, colour::secondary);
        setColour (juce::PopupMenu::backgroundColourId, colour::lifted);
        setColour (juce::PopupMenu::textColourId, colour::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::hairline);
        setColour (juce::PopupMenu::highlightedTextColourId, colour::text);
        setColour (juce::TextButton::textColourOffId, colour::label);
        setColour (juce::TextButton::textColourOnId, colour::surface);
        setColour (juce::Label::textColourId, colour::text);
        setColour (juce::TooltipWindow::backgroundColourId, colour::lifted);
        setColour (juce::TooltipWindow::textColourId, colour::text);
        setColour (juce::TooltipWindow::outlineColourId, colour::hairline);
    }

    void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                 float pos, float startAngle, float endAngle, juce::Slider& s)
    {
        const bool small = (bool) s.getProperties().getWithDefault ("small", false);
        const auto accent = juce::Colour ((juce::uint32) (juce::int64) s.getProperties().getWithDefault ("accent", (juce::int64) colour::wet.getARGB()));

        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float size = juce::jmin (b.getWidth(), b.getHeight());
        b = b.withSizeKeepingCentre (size, size);
        const auto c = b.getCentre();

        const float stroke = small ? 3.0f : 4.5f;
        const float arcR = size * 0.5f - stroke * 0.5f - 1.0f;

        //  Track.
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colour::track);
        g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        //  Value arc.
        const float zero = (float) (double) s.getProperties().getWithDefault ("bipolar", -1.0);
        const float from = zero >= 0.0f ? startAngle + zero * (endAngle - startAngle) : startAngle;
        const float to   = startAngle + pos * (endAngle - startAngle);
        if (std::abs (to - from) > 0.002f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
            g.setColour (s.isEnabled() ? accent : accent.withAlpha (0.4f));
            g.strokePath (arc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        //  Face.
        const float faceR = arcR - stroke * 0.5f - (small ? 3.0f : 5.0f);
        g.setColour (colour::knobFace);
        g.fillEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
        g.setColour (colour::knobRim);
        g.drawEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f, 1.0f);

        //  Pointer.
        const float a = to;
        const float r0 = faceR * 0.52f, r1 = faceR * 0.86f;
        const juce::Point<float> p0 (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a));
        const juce::Point<float> p1 (c.x + r1 * std::sin (a), c.y - r1 * std::cos (a));
        g.setColour (colour::text);
        g.drawLine ({ p0, p1 }, small ? 1.6f : 2.0f);

        if (s.hasKeyboardFocus (false))
        {
            g.setColour (colour::secondary.withAlpha (0.6f));
            g.drawEllipse (c.x - faceR - 2.0f, c.y - faceR - 2.0f, faceR * 2.0f + 4.0f, faceR * 2.0f + 4.0f, 1.0f);
        }
    }

    void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        if (on)
        {
            g.setColour (colour::wet);
            g.fillRoundedRectangle (r, 5.0f);
            return;
        }
        g.setColour (down ? colour::hairline : over ? colour::lifted.brighter (0.08f) : colour::surface);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (over ? colour::secondary : colour::hairline.brighter (0.25f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        const bool on = b.getToggleState();
        g.setColour (on ? colour::surface : colour::label);
        g.setFont (font (11.0f, on, 0.12f));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    void Look::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (down ? colour::hairline : box.isMouseOver (true) ? colour::lifted.brighter (0.06f) : colour::lifted);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colour::hairline.brighter (0.2f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        //  Chevron.
        const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
        juce::Path p;
        p.startNewSubPath (cx - 4.0f, cy - 2.0f);
        p.lineTo (cx, cy + 2.0f);
        p.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (colour::secondary);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (8, 0, box.getWidth() - 30, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centred);
    }

    juce::Font Look::getComboBoxFont (juce::ComboBox&) { return font (13.0f, false, 0.06f); }
    juce::Font Look::getPopupMenuFont() { return font (13.0f); }

    void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
    {
        g.fillAll (colour::lifted);
        g.setColour (colour::hairline);
        g.drawRect (0, 0, w, h, 1);
    }

    void Look::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                  bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                  bool, const juce::String& text, const juce::String&,
                                  const juce::Drawable*, const juce::Colour*)
    {
        if (isSeparator)
        {
            g.setColour (colour::hairline);
            g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }
        if (isHighlighted && isActive)
        {
            g.setColour (colour::hairline);
            g.fillRect (area);
        }
        auto r = area.reduced (12, 0);
        if (isTicked)
        {
            g.setColour (colour::wet);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ (float) r.getX() + 2.5f, (float) area.getCentreY() }));
        }
        g.setColour (isActive ? colour::text : colour::secondary);
        g.setFont (getPopupMenuFont());
        g.drawText (text, r.withTrimmedLeft (12), juce::Justification::centredLeft, true);
    }
}
