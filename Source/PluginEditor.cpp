#include "PluginEditor.h"

#include <AfterHitVersion.h>

#include "Core/Presets.h"

using namespace ah::ui;

namespace
{
    const char* const kBigIds[]    = { ah::id::hit, ah::id::space, ah::id::tail, ah::id::gate };
    const char* const kBigNames[]  = { "HIT", "SPACE", "TAIL", "GATE" };
    const char* const kSmallIds[]  = { ah::id::after, ah::id::sensitivity, ah::id::tone, ah::id::width, ah::id::output };
    const char* const kSmallNames[]= { "AFTER", "SENSITIVITY", "TONE", "WIDTH", "OUTPUT" };
    //  Drawer columns, left to right; SYNC sits between WIDTH and OUTPUT.
    constexpr int kDrawerColumns = 6;
    constexpr int kSyncColumn = 4;

    const char* const kTips[] = {
        "HIT - attack emphasis on the dry hit (0% = untouched)",
        "SPACE - how much of the room is heard",
        "TAIL - reverb decay time (T60)",
        "GATE - how long the room stays audible after each hit",
        "AFTER - protected gap between the hit and its room",
        "SENSITIVITY - how easily a hit is detected",
        "TONE - dark to bright, room only",
        "WIDTH - stereo width of the room only",
        "OUTPUT - final trim" };

    float decayTowards (float shown, float now, float perTick) { return now >= shown ? now : juce::jmax (now, shown * perTick); }

    //  -48..0 dBFS onto 0..1 for the meters.
    float meterPos (float a)
    {
        if (a <= 0.0f) return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (a) + 48.0f) / 48.0f);
    }
}

// --- Dial ---------------------------------------------------------------------
Dial::Dial (juce::RangedAudioParameter& p)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), param (p)
{
    setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    setMouseDragSensitivity (220);
    setWantsKeyboardFocus (true);
    setScrollWheelEnabled (true);
    setTitle (p.getName (64));
}

void Dial::mouseDown (const juce::MouseEvent& e)
{
    //  Shift held at the start of a drag = fine adjustment for the whole drag.
    setMouseDragSensitivity (e.mods.isShiftDown() ? 1200 : 220);
    juce::Slider::mouseDown (e);
}

bool Dial::keyPressed (const juce::KeyPress& k)
{
    const bool fine = k.getModifiers().isShiftDown();
    double step = 0.0;
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)        step = fine ? 0.002 : 0.01;
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)  step = fine ? -0.002 : -0.01;
    else if (code == juce::KeyPress::pageUpKey)                                   step = 0.1;
    else if (code == juce::KeyPress::pageDownKey)                                 step = -0.1;
    else if (code == juce::KeyPress::homeKey || code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        param.beginChangeGesture();
        setValue (param.convertFrom0to1 (param.getDefaultValue()), juce::sendNotificationSync);
        param.endChangeGesture();
        return true;
    }
    else
        return juce::Slider::keyPressed (k);

    const double pos = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + step);
    param.beginChangeGesture();
    setValue (proportionOfLengthToValue (pos), juce::sendNotificationSync);
    param.endChangeGesture();
    return true;
}

// --- editor -------------------------------------------------------------------
AfterHitAudioProcessorEditor::AfterHitAudioProcessorEditor (AfterHitAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), timingStrip (p.getState(), p.getTelemetry())
{
    setLookAndFeel (&look);
    auto& st = p.getState();

    addAndMakeVisible (timingStrip);

    for (int i = 0; i < kNumBig; ++i)
    {
        auto* param = st.getParameter (kBigIds[i]);
        auto& d = bigDials[(size_t) i];
        d = std::make_unique<Dial> (*param);
        d->getProperties().set ("accent", (juce::int64) (i == 0 ? colour::dry : colour::wet).getARGB());
        d->setTooltip (kTips[i]);
        addAndMakeVisible (*d);
        sliderAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, kBigIds[i], *d);
        d->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }
    for (int i = 0; i < kNumSmall; ++i)
    {
        auto* param = st.getParameter (kSmallIds[i]);
        auto& d = smallDials[(size_t) i];
        d = std::make_unique<Dial> (*param);
        d->getProperties().set ("small", true);
        d->getProperties().set ("accent", (juce::int64) (i == 4 ? colour::dry : colour::wet).getARGB());
        if (i == 4)   // OUTPUT grows from 0 dB
            d->getProperties().set ("bipolar", (double) param->convertTo0to1 (0.0f));
        d->setTooltip (kTips[kNumBig + i]);
        addChildComponent (*d);
        sliderAttachments[(size_t) (kNumBig + i)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, kSmallIds[i], *d);
        d->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }

    syncButton.setClickingTogglesState (true);
    syncButton.onStateChange = [this] { syncButton.setButtonText (syncButton.getToggleState() ? "ON" : "OFF"); };
    syncButton.setTooltip ("SYNC - GATE follows the host tempo in note divisions");
    addChildComponent (syncButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ah::id::sync, syncButton);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass (click-free)");
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ah::id::bypass, bypassButton);

    for (int i = 0; i < ah::presets::count(); ++i)
        presets.addItem (ah::presets::name (i).toUpperCase(), i + 1);
    presets.setSelectedId (p.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.setTitle ("Preset");
    presets.onChange = [this]
    {
        const int idx = presets.getSelectedId() - 1;
        if (idx >= 0)
            processor.applyPreset (idx);
    };
    addAndMakeVisible (presets);

    advancedButton.setToggleState (p.advancedOpen.load(), juce::dontSendNotification);
    advancedButton.setTooltip ("Show / hide the advanced controls");
    advancedButton.onClick = [this] { setAdvancedOpen (advancedButton.getToggleState()); };
    addAndMakeVisible (advancedButton);

    advancedOpen = ! p.advancedOpen.load();   // force the first layout
    setAdvancedOpen (p.advancedOpen.load());

    refreshValues();
    startTimerHz (30);
}

AfterHitAudioProcessorEditor::~AfterHitAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void AfterHitAudioProcessorEditor::setAdvancedOpen (bool open)
{
    if (open == advancedOpen)
        return;
    advancedOpen = open;
    processor.advancedOpen.store (open);
    advancedButton.setToggleState (open, juce::dontSendNotification);
    for (auto& d : smallDials) d->setVisible (open);
    syncButton.setVisible (open);
    setSize (metric::width, open ? metric::openHeight : metric::closedHeight);
    repaint();
}

void AfterHitAudioProcessorEditor::resized()
{
    using namespace metric;
    const int w = getWidth();

    //  Top bar.
    presets.setBounds (w / 2 - 94, 6, 188, 24);
    bypassButton.setBounds (w - gutter - 70, 6, 70, 24);

    timingStrip.setBounds (0, topBar, w, metric::strip);

    //  Performance row: four equal columns. These positions never depend on
    //  the drawer.
    const int rowY = topBar + metric::strip;
    const int col = w / kNumBig;
    for (int i = 0; i < kNumBig; ++i)
        bigDials[(size_t) i]->setBounds (col * i + col / 2 - bigDial / 2, rowY + 26, bigDial, bigDial);

    //  Bottom bar.
    const int bottomY = rowY + performance;
    advancedButton.setBounds (gutter - 4, bottomY + 2, 118, bottomBar - 4);

    //  Drawer, under the bottom bar.
    const int dY = bottomY + bottomBar;
    const float dcol = (float) (w - 2 * gutter) / (float) kDrawerColumns;
    auto colCentre = [&] (int c) { return gutter + (int) std::round (dcol * ((float) c + 0.5f)); };
    for (int i = 0; i < kNumSmall; ++i)
    {
        const int c = i < kSyncColumn ? i : i + 1;
        smallDials[(size_t) i]->setBounds (colCentre (c) - smallDial / 2, dY + 20, smallDial, smallDial);
    }
    syncButton.setBounds (colCentre (kSyncColumn) - 30, dY + 27, 60, 22);
}

void AfterHitAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace metric;
    const int w = getWidth();
    const auto wf = (float) w;

    g.fillAll (colour::surface);

    // --- top bar ------------------------------------------------------------------
    drawLogoMark (g, { (float) gutter, 9.0f, 18.0f, 18.0f });
    g.setColour (colour::text);
    g.setFont (font (16.0f, true, 0.20f));
    g.drawText ("AFTERHIT", juce::Rectangle<float> ((float) gutter + 28.0f, 0.0f, 160.0f, (float) topBar), juce::Justification::centredLeft, false);

    // --- bands -----------------------------------------------------------------------
    const float rowY = (float) (topBar + strip);
    const float bottomY = rowY + (float) performance;
    g.setColour (colour::lifted);
    g.fillRect (juce::Rectangle<float> (0.0f, rowY, wf, (float) performance));
    if (advancedOpen)
        g.fillRect (juce::Rectangle<float> (0.0f, bottomY + (float) bottomBar, wf, (float) drawer));

    g.setColour (colour::hairline);
    for (float y : { (float) topBar, rowY, bottomY, bottomY + (float) bottomBar })
        if (y < (float) getHeight())
            g.fillRect (juce::Rectangle<float> (0.0f, y - 0.5f, wf, 1.0f));

    // --- performance row: names above, values below ----------------------------------
    const float col = wf / kNumBig;
    for (int i = 0; i < kNumBig; ++i)
    {
        const float cx = col * (float) i + col * 0.5f;
        g.setColour (colour::label);
        g.setFont (font (12.5f, true, 0.16f));
        g.drawText (kBigNames[i], juce::Rectangle<float> (cx - 70.0f, rowY + 7.0f, 140.0f, 16.0f), juce::Justification::centred, false);
        g.setColour (colour::text);
        g.setFont (numberFont (15.0f));
        g.drawText (values[(size_t) i], juce::Rectangle<float> (cx - 70.0f, rowY + 88.0f, 140.0f, 18.0f), juce::Justification::centred, false);
    }

    // --- bottom bar ------------------------------------------------------------------
    {
        const float y = bottomY;
        g.setColour (advancedButton.isMouseOver() ? colour::text : colour::label);
        g.setFont (font (12.0f, false, 0.14f));
        g.drawText ("ADVANCED", juce::Rectangle<float> ((float) gutter, y, 90.0f, (float) bottomBar), juce::Justification::centredLeft, false);
        const float cx = (float) gutter + 96.0f, cy = y + (float) bottomBar * 0.5f;
        juce::Path chev;
        if (advancedOpen) { chev.startNewSubPath (cx - 4.5f, cy + 2.0f); chev.lineTo (cx, cy - 2.5f); chev.lineTo (cx + 4.5f, cy + 2.0f); }
        else              { chev.startNewSubPath (cx - 4.5f, cy - 2.0f); chev.lineTo (cx, cy + 2.5f); chev.lineTo (cx + 4.5f, cy - 2.0f); }
        g.setColour (colour::wet);
        g.strokePath (chev, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (colour::secondary);
        g.setFont (font (10.0f, false, 0.08f));
        g.drawText (juce::String ("v") + ah::kVersion, juce::Rectangle<float> (wf - (float) gutter - 40.0f, y, 40.0f, (float) bottomBar),
                    juce::Justification::centredRight, false);
        g.setFont (font (10.5f, false, 0.12f));
        g.drawText ("OUT", juce::Rectangle<float> (wf - (float) gutter - 196.0f, y, 32.0f, (float) bottomBar), juce::Justification::centredLeft, false);
    }

    // --- drawer ----------------------------------------------------------------------
    if (advancedOpen)
    {
        const float dY = bottomY + (float) bottomBar;
        const float dcol = (wf - 2.0f * (float) gutter) / (float) kDrawerColumns;
        for (int c = 0; c < kDrawerColumns; ++c)
        {
            const float cx = (float) gutter + dcol * ((float) c + 0.5f);
            const bool isSync = c == kSyncColumn;
            const int i = c < kSyncColumn ? c : c - 1;
            g.setColour (colour::label);
            g.setFont (font (10.0f, true, 0.12f));
            g.drawText (isSync ? "SYNC" : kSmallNames[i], juce::Rectangle<float> (cx - 52.0f, dY + 5.0f, 104.0f, 13.0f), juce::Justification::centred, false);
            g.setColour (isSync ? colour::secondary : colour::text);
            g.setFont (numberFont (isSync ? 10.5f : 12.0f));
            g.drawText (isSync ? bpmText : values[(size_t) (kNumBig + i)], juce::Rectangle<float> (cx - 52.0f, dY + 59.0f, 104.0f, 14.0f),
                        juce::Justification::centred, false);
        }
    }

    paintMeters (g);
}

void AfterHitAudioProcessorEditor::paintMeters (juce::Graphics& g)
{
    using namespace metric;
    const float wf = (float) getWidth();

    //  Header: wet pair (amber) then dry pair (off-white), L/R each.
    {
        const float x0 = wf - (float) gutter - 70.0f - 14.0f - 30.0f;
        const float top = 10.0f, h = 16.0f, bw = 4.0f;
        const float vals[] = { mWetL, mWetR, mDryL, mDryR };
        for (int i = 0; i < 4; ++i)
        {
            const float x = x0 + (float) i * (bw + 2.0f) + (i >= 2 ? 6.0f : 0.0f);
            g.setColour (colour::hairline);
            g.fillRoundedRectangle (x, top, bw, h, 1.5f);
            const float fh = h * meterPos (vals[i]);
            if (fh > 0.5f)
            {
                g.setColour (i < 2 ? colour::wet : colour::dry);
                g.fillRoundedRectangle (x, top + h - fh, bw, fh, 1.5f);
            }
        }
    }

    //  Bottom: OUT bar with a latched clip tip (click to clear).
    {
        const float y = (float) (topBar + strip + performance) + (float) bottomBar * 0.5f - 3.0f;
        const float x = wf - (float) gutter - 160.0f, w = 110.0f;
        g.setColour (colour::hairline);
        g.fillRoundedRectangle (x, y, w, 6.0f, 3.0f);
        const float fw = w * meterPos (mOut);
        if (fw > 1.0f)
        {
            g.setColour (colour::wet);
            g.fillRoundedRectangle (x, y, fw, 6.0f, 3.0f);
        }
        if (clipShown)
        {
            g.setColour (colour::clip);
            g.fillRoundedRectangle (x + w + 3.0f, y, 6.0f, 6.0f, 3.0f);
        }
    }
}

void AfterHitAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    //  Clicking the OUT meter clears the clip latch.
    const float y = (float) (metric::topBar + metric::strip + metric::performance);
    const juce::Rectangle<float> outArea ((float) getWidth() - (float) metric::gutter - 200.0f, y, 150.0f, (float) metric::bottomBar);
    if (outArea.contains (e.position))
    {
        processor.getTelemetry().clipped.store (false);
        clipShown = false;
        repaint();
    }
}

void AfterHitAudioProcessorEditor::refreshValues()
{
    auto& st = processor.getState();
    auto get = [&st] (const char* pid) { return st.getRawParameterValue (pid)->load(); };
    const auto& t = processor.getTelemetry();
    const double bpm = t.bpm.load();
    const bool sync = get (ah::id::sync) > 0.5f;

    std::array<juce::String, kNumBig + kNumSmall> v {
        ah::formatPercent (get (ah::id::hit)),
        ah::formatPercent (get (ah::id::space)),
        ah::formatTail (get (ah::id::tail)),
        ah::formatGate (get (ah::id::gate), sync, bpm),
        ah::formatMs (get (ah::id::after)),
        ah::formatPercent (get (ah::id::sensitivity)),
        ah::formatPercent (get (ah::id::tone)),
        ah::formatPercent (get (ah::id::width)),
        ah::formatDb (get (ah::id::output)) };

    const auto bpmNow = juce::String (juce::roundToInt (bpm)) + (t.bpmFromHost.load() ? " BPM" : " BPM (no host)");
    if (v != values || bpmNow != bpmText)
    {
        values = v;
        bpmText = bpmNow;
        repaint();
    }

    //  GATE's dial tooltip names the actual window.
    const float win = t.windowMs.load();
    bigDials[3]->setTooltip (win > 0.0f ? juce::String (kTips[3]) + "  (now " + ah::formatMs (win) + ")" : juce::String (kTips[3]) + "  (off)");
}

void AfterHitAudioProcessorEditor::timerCallback()
{
    refreshValues();
    timingStrip.refresh();

    auto& t = processor.getTelemetry();
    constexpr float fall = 0.80f;   // per 33 ms tick: about 20 dB/s... then faster below
    auto take = [] (std::atomic<float>& a) { return a.exchange (0.0f, std::memory_order_relaxed); };
    const float dl = take (t.dryL), dr = take (t.dryR), wl = take (t.wetL), wr = take (t.wetR);
    const float ol = take (t.outL), orr = take (t.outR);
    const float nDryL = decayTowards (mDryL, dl, fall), nDryR = decayTowards (mDryR, dr, fall);
    const float nWetL = decayTowards (mWetL, wl, fall), nWetR = decayTowards (mWetR, wr, fall);
    const float nOut  = decayTowards (mOut, juce::jmax (ol, orr), fall);
    const bool clip = t.clipped.load();
    const bool changed = std::abs (meterPos (nDryL) - meterPos (mDryL)) > 0.004f || std::abs (meterPos (nDryR) - meterPos (mDryR)) > 0.004f
                      || std::abs (meterPos (nWetL) - meterPos (mWetL)) > 0.004f || std::abs (meterPos (nWetR) - meterPos (mWetR)) > 0.004f
                      || std::abs (meterPos (nOut) - meterPos (mOut)) > 0.004f || clip != clipShown;
    mDryL = nDryL; mDryR = nDryR; mWetL = nWetL; mWetR = nWetR; mOut = nOut; clipShown = clip;
    if (changed)
    {
        const int w = getWidth();
        repaint (w - metric::gutter - 130, 6, 60, 26);
        repaint (w - metric::gutter - 200, metric::topBar + metric::strip + metric::performance, 190, metric::bottomBar);
    }

    const int prog = processor.getCurrentProgram();
    if (presets.getSelectedId() != prog + 1)
        presets.setSelectedId (prog + 1, juce::dontSendNotification);
    const auto shown = ah::presets::name (prog).toUpperCase() + (ah::presets::matches (prog, processor.getState()) ? "" : " *");
    if (presets.getText() != shown)
        presets.setText (shown, juce::dontSendNotification);
}
