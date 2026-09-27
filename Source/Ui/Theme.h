// The ONLY place AFTERHIT's colours, metrics and type live.
//
// Colour law: the dry hit is off-white; everything wet - SPACE, TAIL, GATE,
// the bloom in the strip - is one restrained copper-amber. The panel is
// graphite. No other hues, no glow, no gradients beyond the bloom's fill.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ah::ui
{
    namespace colour
    {
        inline const juce::Colour surface   { 0xff101417 };   // top bar, strip, bottom bar
        inline const juce::Colour lifted    { 0xff181E21 };   // controls region
        inline const juce::Colour hairline  { 0xff2C3539 };
        inline const juce::Colour text      { 0xffE8ECE9 };
        inline const juce::Colour secondary { 0xff879399 };
        inline const juce::Colour label     { 0xffC7CFD1 };   // dial names: between text and secondary
        inline const juce::Colour wet       { 0xffF49A58 };   // copper-amber
        inline const juce::Colour dry       { 0xffE8ECE9 };   // = text
        inline const juce::Colour knobFace  { 0xff1D2427 };
        inline const juce::Colour knobRim   { 0xff3A4549 };
        inline const juce::Colour track     { 0xff2C3539 };
        inline const juce::Colour protect   { 0xff1C2326 };   // the strip's protected gap
        inline const juce::Colour clip      { 0xffE5484D };
    }

    namespace metric
    {
        inline constexpr int width        = 640;
        inline constexpr int topBar       = 36;
        inline constexpr int strip        = 54;
        inline constexpr int performance  = 108;
        inline constexpr int bottomBar    = 28;
        inline constexpr int drawer       = 76;
        inline constexpr int closedHeight = topBar + strip + performance + bottomBar;   // 226
        inline constexpr int openHeight   = closedHeight + drawer;                     // 302

        inline constexpr int gutter    = 16;
        inline constexpr int bigDial   = 60;
        inline constexpr int smallDial = 38;
    }

    //  Clear system sans (SF on macOS, Segoe UI on Windows) - no bundled font,
    //  so nothing to license. Numbers use tabular figures so a value does not
    //  shimmy sideways while its dial turns.
    juce::Font font (float height, bool bold = false, float tracking = 0.0f);
    juce::Font numberFont (float height);

    //  The mark: five bars - two off-white (the hit), three amber (what
    //  follows it). Fills `bounds`.
    void drawLogoMark (juce::Graphics&, juce::Rectangle<float> bounds);

    //  The maker's mark - Naaman's N in a circle, bone strokes and one brass
    //  diagonal. Same geometry as ISO, FOUR COLOR and the website (a 40x40
    //  box: verticals at x 13.2 / 26.8 from y 12.6 to 27.4, circle r 18.25).
    void drawNaamanMark (juce::Graphics&, juce::Rectangle<float> bounds);

    //  Text with letter spacing, drawn in `area`.
    void drawTracked (juce::Graphics&, const juce::String&, juce::Rectangle<float> area,
                      juce::Font, juce::Justification);

    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look();

        //  Per-dial variation travels in the Slider's properties:
        //    "accent"  ARGB of the value arc
        //    "small"   true for the ADVANCED drawer's dials
        //    "bipolar" arc grows from the travel's `zero` position (0..1)
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                               float pos, float startAngle, float endAngle, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                                bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                const juce::Drawable* icon, const juce::Colour* textColour) override;
    };
}
