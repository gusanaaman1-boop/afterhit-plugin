#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "Ui/Theme.h"
#include "Ui/TimingStrip.h"

namespace ah::ui
{
    //  A rotary dial: vertical drag (shift = fine), double-click = default,
    //  arrow keys / page keys when focused (shift = fine). Every change is a
    //  proper host gesture, so Cubase can undo it.
    class Dial : public juce::Slider
    {
    public:
        explicit Dial (juce::RangedAudioParameter& p);
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
    private:
        juce::RangedAudioParameter& param;
    };

    //  The "ADVANCED v" text in the bottom bar is drawn by the editor; this is
    //  its (invisible) hit area, focusable and accessible.
    class Disclosure : public juce::Button
    {
    public:
        Disclosure() : juce::Button ("Advanced") { setClickingTogglesState (true); setWantsKeyboardFocus (true); }
        void paintButton (juce::Graphics&, bool, bool) override {}
    };
}

class AfterHitAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit AfterHitAudioProcessorEditor (AfterHitAudioProcessor&);
    ~AfterHitAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    //  Test hooks.
    bool isAdvancedOpen() const noexcept { return advancedOpen; }
    void setAdvancedOpen (bool);
    juce::Rectangle<int> mainDialBounds (int i) const { return bigDials[(size_t) i]->getBounds(); }
    juce::String valueText (int i) const { return values[(size_t) i]; }
    juce::ComboBox& presetBox() noexcept { return presets; }
    void pollNow() { timerCallback(); }

    static constexpr int kNumBig = 4, kNumSmall = 5;   // + SYNC button in the drawer

private:
    void timerCallback() override;
    void refreshValues();
    void paintMeters (juce::Graphics&);

    AfterHitAudioProcessor& processor;
    ah::ui::Look look;

    ah::ui::TimingStrip timingStrip;

    std::array<std::unique_ptr<ah::ui::Dial>, kNumBig> bigDials;
    std::array<std::unique_ptr<ah::ui::Dial>, kNumSmall> smallDials;   // AFTER SENS TONE WIDTH OUTPUT
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, kNumBig + kNumSmall> sliderAttachments;

    juce::TextButton syncButton { "SYNC" }, bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment, bypassAttachment;
    ah::ui::Disclosure advancedButton;
    juce::ComboBox presets;

    std::array<juce::String, kNumBig + kNumSmall> values;
    juce::String bpmText;
    bool advancedOpen = false;

    //  Meter display state (peak with fall-off), in linear amplitude.
    float mDryL = 0, mDryR = 0, mWetL = 0, mWetR = 0, mOut = 0;
    bool clipShown = false;

    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterHitAudioProcessorEditor)
};
