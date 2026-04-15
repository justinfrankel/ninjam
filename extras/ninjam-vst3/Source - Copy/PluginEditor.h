#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class NinjamVst3AudioProcessorEditor;  // forward declaration for LAF classes

#if JUCE_WINDOWS
struct WinVideoReader;  // Windows Media Foundation frame reader (defined in PluginEditor.cpp)
#endif

class IntervalDisplayComponent : public juce::Component
{
public:
    IntervalDisplayComponent(NinjamVst3AudioProcessor& p) : processor(p) {}

    void paint(juce::Graphics& g) override
    {
        int bpi = processor.getBPI();
        if (bpi <= 0)
            bpi = 4;

        const float progress = juce::jlimit(0.0f, 1.0f, processor.getIntervalProgress());
        const float totalBeats = progress * (float)bpi;
        const int currentBeat = (int)totalBeats;

        auto bounds = getLocalBounds().toFloat();
        const float blockWidth = bounds.getWidth() / (float)bpi;
        const float blockHeight = bounds.getHeight();

        const juce::Colour onColor = juce::Colour(0xFFFFFDD0);
        const juce::Colour offColor = juce::Colours::black.withAlpha(0.3f);

        for (int i = 0; i < bpi; ++i)
        {
            auto blockArea = juce::Rectangle<float>(i * blockWidth, 0.0f, blockWidth, blockHeight).reduced(2.0f);
            if (i < currentBeat)
            {
                g.setColour(onColor);
                g.fillRect(blockArea);
            }
            else if (i == currentBeat && currentBeat < bpi)
            {
                const float subBeat = totalBeats - (float)currentBeat;
                const float alpha = 0.6f + 0.4f * std::sin(subBeat * juce::MathConstants<float>::pi);
                g.setColour(onColor.withAlpha(alpha));
                g.fillRect(blockArea);
            }
            else
            {
                g.setColour(offColor);
                g.drawRect(blockArea, 1.0f);
            }
        }
    }

private:
    NinjamVst3AudioProcessor& processor;
};

class OutlinedLabelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        if (!label.isBeingEdited())
        {
            auto alpha = label.isEnabled() ? 1.0f : 0.5f;
            auto font  = getLabelFont(label);
            g.setFont(font);

            auto textArea = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
            juce::String text = label.getText();
            if (text.isEmpty()) return;

            auto just = label.getJustificationType();

            // black outline: draw at radius-1 and radius-2 for heavier weight
            g.setColour(juce::Colours::black.withAlpha(alpha * 0.80f));
            for (int r = 1; r <= 2; ++r)
                for (int dx = -r; dx <= r; ++dx)
                    for (int dy = -r; dy <= r; ++dy)
                        if (dx != 0 || dy != 0)
                            g.drawFittedText(text,
                                             textArea.translated(dx, dy),
                                             just, 1, 1.0f);

            // main text on top
            g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
            g.drawFittedText(text, textArea, just, 1, 1.0f);
        }
        else
        {
            LookAndFeel_V4::drawLabel(g, label);
        }
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        // Draw the tick box using the default implementation first (painted separately)
        auto tickWidth = juce::jmin(20.0f, (float)button.getHeight() * 0.8f);
        drawTickBox(g, button, 4.0f, ((float)button.getHeight() - tickWidth) * 0.5f,
                    tickWidth, tickWidth, button.getToggleState(),
                    button.isEnabled(), shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        // Draw label text with black outline
        auto alpha  = button.isEnabled() ? 1.0f : 0.5f;
        auto textX  = (int)(tickWidth + 8.0f);
        auto textArea = button.getLocalBounds().withTrimmedLeft(textX);
        juce::String text = button.getButtonText();
        g.setFont(13.0f);

        g.setColour(juce::Colours::black.withAlpha(alpha * 0.80f));
        for (int r = 1; r <= 2; ++r)
            for (int dx = -r; dx <= r; ++dx)
                for (int dy = -r; dy <= r; ++dy)
                    if (dx != 0 || dy != 0)
                        g.drawFittedText(text, textArea.translated(dx, dy),
                                         juce::Justification::centredLeft, 1, 1.0f);

        g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(alpha));
        g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1, 1.0f);
    }
};

class FaderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle, juce::Slider&) override;
    void drawLinearSliderBackground(juce::Graphics&, int x, int y, int width, int height,
                                    float sliderPos, float minSliderPos, float maxSliderPos,
                                    const juce::Slider::SliderStyle, juce::Slider&) override;
};

class NonlinearFaderSlider : public juce::Slider
{
public:
    NonlinearFaderSlider() = default;

    double valueToProportionOfLength(double value) override
    {
        auto range = getRange();
        double minV = range.getStart();
        double maxV = range.getEnd();
        if (maxV <= minV)
            return 0.0;

        double norm = (value - minV) / (maxV - minV);
        norm = juce::jlimit(0.0, 1.0, norm);

        double midNorm = (1.0 - minV) / (maxV - minV);
        double p0 = 0.8;

        if (norm <= midNorm)
        {
            if (midNorm <= 0.0)
                return 0.0;
            double p = (norm / midNorm) * p0;
            return p;
        }
        else
        {
            double xProp = (norm - midNorm) / (1.0 - midNorm);
            double p = p0 + xProp * (1.0 - p0);
            return p;
        }
    }

    double proportionOfLengthToValue(double proportion) override
    {
        auto range = getRange();
        double minV = range.getStart();
        double maxV = range.getEnd();
        if (maxV <= minV)
            return minV;

        double p = juce::jlimit(0.0, 1.0, (double)proportion);
        double midNorm = (1.0 - minV) / (maxV - minV);
        double p0 = 0.8;

        double norm;
        if (p <= p0)
        {
            if (p0 <= 0.0)
                norm = 0.0;
            else
                norm = (p / p0) * midNorm;
        }
        else
        {
            double xProp = (p - p0) / (1.0 - p0);
            norm = midNorm + xProp * (1.0 - midNorm);
        }

        return minV + norm * (maxV - minV);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        leftInteractionActive = e.mods.isLeftButtonDown();
        if (leftInteractionActive)
            juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (leftInteractionActive)
            juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (leftInteractionActive)
            juce::Slider::mouseUp(e);
        leftInteractionActive = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown())
            juce::Slider::mouseDoubleClick(e);
    }

private:
    bool leftInteractionActive = false;
};

class LeftClickOnlySlider : public juce::Slider
{
public:
    using juce::Slider::Slider;

    void mouseDown(const juce::MouseEvent& e) override
    {
        leftInteractionActive = e.mods.isLeftButtonDown();
        if (leftInteractionActive)
            juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (leftInteractionActive)
            juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (leftInteractionActive)
            juce::Slider::mouseUp(e);
        leftInteractionActive = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown())
            juce::Slider::mouseDoubleClick(e);
    }

private:
    bool leftInteractionActive = false;
};

class LeftClickOnlyTextButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    void mouseDown(const juce::MouseEvent& e) override
    {
        leftInteractionActive = e.mods.isLeftButtonDown();
        if (leftInteractionActive)
            juce::TextButton::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (leftInteractionActive)
            juce::TextButton::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (leftInteractionActive)
            juce::TextButton::mouseUp(e);
        leftInteractionActive = false;
    }

private:
    bool leftInteractionActive = false;
};

class LeftClickOnlyToggleButton : public juce::ToggleButton
{
public:
    using juce::ToggleButton::ToggleButton;

    void mouseDown(const juce::MouseEvent& e) override
    {
        leftInteractionActive = e.mods.isLeftButtonDown();
        if (leftInteractionActive)
            juce::ToggleButton::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (leftInteractionActive)
            juce::ToggleButton::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (leftInteractionActive)
            juce::ToggleButton::mouseUp(e);
        leftInteractionActive = false;
    }

private:
    bool leftInteractionActive = false;
};

class MuteSoloBtnLookAndFeel : public juce::LookAndFeel_V4
{
public:
    bool isMute = true; // true = red (mute), false = yellow-orange (solo)

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        bool isOn = button.getToggleState();
        const float r = 4.0f;

        juce::Colour bg, rim;
        if (isMute)
        {
            bg  = isOn ? juce::Colour::fromRGB(130, 20, 20) : juce::Colour::fromRGB(35, 8, 8);
            rim = isOn ? juce::Colour::fromRGB(255, 80, 80)
                       : juce::Colour::fromRGB(255, 80, 80).withAlpha(0.25f);
        }
        else
        {
            bg  = isOn ? juce::Colour::fromRGB(155, 100, 5) : juce::Colour::fromRGB(42, 25, 3);
            rim = isOn ? juce::Colour::fromRGB(255, 210, 60)
                       : juce::Colour::fromRGB(255, 210, 60).withAlpha(0.25f);
        }

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, r);
        g.setColour(rim);
        g.drawRoundedRectangle(bounds, r, 1.5f);

        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRoundedRectangle(bounds, r);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        bool isOn = button.getToggleState();
        auto bounds = button.getLocalBounds();
        float fontSize = juce::jmin(14.0f, (float)bounds.getHeight() * 0.65f);
        g.setFont(juce::Font(fontSize, juce::Font::bold));

        juce::Colour tc = isOn ? juce::Colours::white : juce::Colours::white.withAlpha(0.30f);

        g.setColour(juce::Colours::black.withAlpha(0.75f));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    g.drawFittedText(button.getButtonText(), bounds.translated(dx, dy),
                                     juce::Justification::centred, 1);
        g.setColour(tc);
        g.drawFittedText(button.getButtonText(), bounds, juce::Justification::centred, 1);
    }
};

class UserChannelStrip : public juce::Component, public juce::Timer
{
public:
    UserChannelStrip(NinjamVst3AudioProcessor& p, int userIdx);
    ~UserChannelStrip() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    void updateInfo(const NinjamVst3AudioProcessor::UserInfo& info);
    void setOrientation(bool isHorizontal); // True = Mixer layout (Strip is vertical), False = List layout (Strip is horizontal)
    void setClipEnabled(bool enabled);
    int getPreferredHeight() const; // For dynamic height in list layout when expanded
    int getPreferredWidth()  const; // For dynamic width in mixer layout when expanded
    int getUserIndex() const;
    juce::Slider& getVolumeSlider();
    juce::Slider& getPanSlider();
    juce::Button& getMuteButton();
    juce::Button& getSoloButton();
    juce::Slider& getChannelSlider(int channel);

private:
    class PanSliderLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              const juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            if (style != juce::Slider::LinearHorizontal)
            {
                juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
                return;
            }

            juce::Rectangle<int> bounds(x, y, width, height);
            int trackHeight = 6;
            juce::Rectangle<int> track(bounds.getX() + 4,
                                       bounds.getCentreY() - trackHeight / 2,
                                       bounds.getWidth() - 8,
                                       trackHeight);

            juce::Colour base = slider.findColour(juce::Slider::backgroundColourId, true);
            if (base == juce::Colour())
                base = juce::Colours::darkgrey.darker();

            g.setColour(base);
            g.fillRect(track);

            g.setColour(juce::Colours::black.withAlpha(0.7f));
            g.drawRect(track);

            double v = slider.getValue();
            double norm = juce::jlimit(-1.0, 1.0, v);

            int centreX = track.getCentreX();

            if (std::abs(norm) > 0.001)
            {
                bool toRight = norm > 0.0;
                float amount = (float)std::abs(norm);
                const int leftEdge = track.getX();
                const int rightEdge = track.getRight();
                const int halfWidth = juce::jmax(1, track.getWidth() / 2);
                const int activeWidth = juce::jmax(1, (int)std::round(halfWidth * amount));

                juce::Rectangle<int> active(track);
                if (toRight)
                {
                    active.setLeft(centreX);
                    active.setRight(juce::jmin(rightEdge, centreX + activeWidth));
                }
                else
                {
                    active.setRight(centreX);
                    active.setLeft(juce::jmax(leftEdge, centreX - activeWidth));
                }

                juce::Colour endColour = toRight ? juce::Colours::red : juce::Colours::white;
                juce::ColourGradient grad(juce::Colours::black, (float)centreX, (float)track.getCentreY(),
                                          endColour, toRight ? (float)active.getRight() : (float)active.getX(), (float)track.getCentreY(), false);

                g.setGradientFill(grad);
                g.setOpacity(1.0f);
                g.fillRect(active);
            }

            int thumbWidth = 6;
            int thumbHeight = trackHeight + 6;
            int thumbX = (int)sliderPos - thumbWidth / 2;
            juce::Rectangle<int> thumb(thumbX, track.getCentreY() - thumbHeight / 2, thumbWidth, thumbHeight);

            g.setColour(juce::Colours::white);
            g.fillRect(thumb);
            g.setColour(juce::Colours::black);
            g.drawRect(thumb);
        }
    };

    NinjamVst3AudioProcessor& processor;
    int userIndex;
    NinjamVst3AudioProcessor::UserInfo userInfo;
    
    juce::Label nameLabel;
    NonlinearFaderSlider volumeSlider;
    LeftClickOnlySlider panSlider;
    PanSliderLookAndFeel panLookAndFeel;
    LeftClickOnlyToggleButton clipButton{"No-Clip"};
    LeftClickOnlyTextButton muteButton{"M"};
    LeftClickOnlyTextButton soloButton{"S"};
    MuteSoloBtnLookAndFeel muteBtnLAF;
    MuteSoloBtnLookAndFeel soloBtnLAF;
    juce::ComboBox outputSelector;
    FaderLookAndFeel faderLookAndFeel;
    juce::Label dbLabel;
    bool showOutputSelector = true;
    
    float currentPeakL = 0.0f;
    float currentPeakR = 0.0f;
    bool isHorizontalLayout = false; // Default List view (strip is horizontal)

    // Multi-channel remote support
    static constexpr int kMaxRemoteCh = 8;
    LeftClickOnlyTextButton expandButton{ ">" };
    bool isExpanded = false;
    int numRemoteChannels = 1;
    bool isMultiChanPeer = false;
    float perChannelGain[kMaxRemoteCh];
    float channelPeaks[kMaxRemoteCh];
    LeftClickOnlySlider channelSliders[kMaxRemoteCh];
    juce::Label  channelNameLabels[kMaxRemoteCh]; // shows remote channel names

    void applyVolumesToProcessor();
    void toggleExpanded();
    void volumeChanged();
    void panChanged();
    void outputChanged();
    void muteChanged();
    void soloChanged();
    void clipChanged();
};

class MasterPeakMeter : public juce::Component
{
public:
    void setPeak(float newPeak)
    {
        peakL = juce::jlimit(0.0f, 1.0f, newPeak);
        peakR = peakL;
        repaint();
    }

    void setPeak(float newPeakL, float newPeakR)
    {
        peakL = juce::jlimit(0.0f, 1.0f, newPeakL);
        peakR = juce::jlimit(0.0f, 1.0f, newPeakR);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.fillAll(juce::Colours::black);

        auto gap = 1;
        auto barWidth = juce::jmax(1, (bounds.getWidth() - gap) / 2);
        auto leftBar = bounds.removeFromLeft(barWidth);
        bounds.removeFromLeft(gap);
        auto rightBar = bounds;

        auto drawBar = [&g] (juce::Rectangle<int> barBounds, float peak)
        {
            float safePeak = juce::jlimit(1.0e-6f, 1.0f, peak);
            float db = 20.0f * std::log10(safePeak);

            juce::Colour colour;
            if (db >= 0.0f) colour = juce::Colours::red;
            else if (db > -6.0f) colour = juce::Colours::yellow;
            else colour = juce::Colours::green;

            int filled = (int)(barBounds.getHeight() * safePeak);
            if (filled <= 0)
                return;

            juce::Rectangle<int> fill(barBounds.getX(), barBounds.getBottom() - filled, barBounds.getWidth(), filled);
            g.setColour(colour);
            g.fillRect(fill);
        };

        drawBar(leftBar, peakL);
        drawBar(rightBar, peakR);
    }

private:
    float peakL = 0.0f;
    float peakR = 0.0f;
};

class UserListComponent : public juce::Component
{
public:
    UserListComponent(NinjamVst3AudioProcessor& p);
    ~UserListComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void updateContent();
    void setLayoutMode(bool horizontal); // True = Horizontal Mixer, False = Vertical List
    void setAllClipEnabled(bool enabled);
    std::vector<UserChannelStrip*> getStripPointers() const;

private:
    NinjamVst3AudioProcessor& processor;
    juce::Viewport viewport;
    juce::Component contentComponent;
    std::vector<std::unique_ptr<UserChannelStrip>> strips;
    bool isHorizontal = false;
};

class CustomKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override;
};

class SyncIconLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        bool isOn = button.getToggleState();
        const float r = 4.0f;

        juce::Colour bg  = isOn ? juce::Colour::fromRGB(110, 60, 10)
                                : juce::Colour::fromRGB(35, 18, 4);
        juce::Colour rim = isOn ? juce::Colour::fromRGB(255, 160, 60)
                                : juce::Colour::fromRGB(255, 160, 60).withAlpha(0.25f);
        juce::Colour ic  = isOn ? juce::Colour::fromRGB(255, 185, 90)
                                : juce::Colour::fromRGB(255, 185, 90).withAlpha(0.22f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, r);
        g.setColour(rim);
        g.drawRoundedRectangle(bounds, r, 1.5f);

        // --- sync / refresh icon: two circular arrows forming a circle ---
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float ir  = bounds.getWidth() * 0.34f;   // arc radius
        float sw  = juce::jmax(1.4f, bounds.getWidth() * 0.115f);
        float ahw = sw * 1.5f;   // arrowhead half-width
        float ahl = sw * 2.0f;   // arrowhead length

        g.setColour(ic);

        using M = juce::MathConstants<float>;
        const float deg = M::pi / 180.0f;

        // Draw arc from startA to endA (clockwise), with filled arrowhead at end
        auto drawArcArrow = [&](float startA, float endA)
        {
            juce::Path arc;
            arc.addCentredArc(cx, cy, ir, ir, 0.0f, startA, endA, true);
            g.strokePath(arc, juce::PathStrokeType(sw, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::butt));

            // Arrowhead tip at end of arc; base recessed along clockwise tangent
            float tipX = cx + std::cos(endA) * ir;
            float tipY = cy + std::sin(endA) * ir;
            float tanA = endA + M::halfPi;   // clockwise tangent direction at endA
            float bx1  = tipX - std::cos(tanA) * ahl - std::cos(endA) * ahw;
            float by1  = tipY - std::sin(tanA) * ahl - std::sin(endA) * ahw;
            float bx2  = tipX - std::cos(tanA) * ahl + std::cos(endA) * ahw;
            float by2  = tipY - std::sin(tanA) * ahl + std::sin(endA) * ahw;
            juce::Path arrowHead;
            arrowHead.startNewSubPath(tipX, tipY);
            arrowHead.lineTo(bx1, by1);
            arrowHead.lineTo(bx2, by2);
            arrowHead.closeSubPath();
            g.fillPath(arrowHead);
        };

        // Arc 1: 210° → 370°(=10°), sweeps clockwise over the TOP of the circle
        drawArcArrow(210.0f * deg, 370.0f * deg);
        // Arc 2:  30° → 190°,       sweeps clockwise over the BOTTOM of the circle
        drawArcArrow(30.0f * deg, 190.0f * deg);

        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRoundedRectangle(bounds, r);
        }
    }
};

class MetronomeButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Colour themeColour { juce::Colour::fromRGB(80, 185, 255) };

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        bool isOn = button.getToggleState();
        const float r = 4.0f;

        juce::Colour bg  = isOn ? themeColour.withMultipliedBrightness(0.55f)
                                : themeColour.withMultipliedBrightness(0.09f);
        juce::Colour rim = isOn ? themeColour
                                : themeColour.withAlpha(0.25f);
        juce::Colour ic  = isOn ? juce::Colours::white
                                : juce::Colours::white.withAlpha(0.30f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, r);
        g.setColour(rim);
        g.drawRoundedRectangle(bounds, r, 1.5f);

        // --- metronome icon (scaled to 72% of button, centred) ---
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float bw = bounds.getWidth()  * 0.72f;
        float bh = bounds.getHeight() * 0.72f;
        float bx = cx - bw * 0.5f;
        float by = cy - bh * 0.5f;

        float sw   = juce::jmax(1.2f, bw * 0.085f);  // stroke width scales with size

        float baseH     = bh * 0.16f;
        float baseY     = by + bh - baseH;
        float bodyBot   = baseY;           // trapezoid bottom (top of base)
        float bodyTop   = by;
        float topRad    = bw * 0.28f;      // half-width at top
        float botRad    = bw * 0.46f;      // half-width at bottom

        // --- outer body: trapezoid with rounded arch top ---
        juce::Path body;
        // arc at top (rounded cap)
        body.startNewSubPath(cx - topRad, bodyTop + topRad * 0.6f);
        body.quadraticTo(cx - topRad, bodyTop,  cx,              bodyTop);
        body.quadraticTo(cx + topRad, bodyTop,  cx + topRad,     bodyTop + topRad * 0.6f);
        // right slant down to base
        body.lineTo(cx + botRad, bodyBot);
        // straight bottom
        body.lineTo(cx - botRad, bodyBot);
        body.closeSubPath();

        g.setColour(ic);
        g.strokePath(body, juce::PathStrokeType(sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // --- solid thick base bar ---
        float baseCorner = juce::jmax(1.0f, baseH * 0.35f);
        g.fillRoundedRectangle(cx - botRad, baseY, botRad * 2.0f, baseH, baseCorner);

        // --- 3 small pill tick marks on left interior ---
        float innerTop  = bodyTop + bh * 0.18f;
        float innerBot  = bodyBot - bh * 0.06f;
        float innerH    = innerBot - innerTop;
        float pillW     = bw * 0.26f;
        float pillH     = juce::jmax(1.5f, bh * 0.075f);
        float pillR     = pillH * 0.5f;
        float pillX     = cx - botRad + (botRad - topRad) * 0.3f + bw * 0.03f;  // left interior
        for (int i = 0; i < 3; ++i)
        {
            float py = innerTop + innerH * (float)i / 2.5f + innerH * 0.05f;
            g.fillRoundedRectangle(pillX, py - pillH * 0.5f, pillW, pillH, pillR);
        }

        // --- pendulum arm: pivots at bottom-centre, swings up to upper-right ---
        float armX0 = cx;
        float armY0 = bodyBot;
        float armX1 = cx + botRad * 0.85f;
        float armY1 = bodyTop + bh * 0.08f;
        g.drawLine(armX0, armY0, armX1, armY1, sw * 1.1f);

        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRoundedRectangle(bounds, r);
        }
    }
};

class ATButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        bool isOn = button.getToggleState();
        const float r = 4.0f;
        juce::Colour bg  = isOn ? juce::Colour::fromRGB(10,  90, 160) : juce::Colour::fromRGB(5, 22, 42);
        juce::Colour rim = isOn ? juce::Colour::fromRGB(80, 185, 255)
                                : juce::Colour::fromRGB(80, 185, 255).withAlpha(0.25f);
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, r);
        g.setColour(rim);
        g.drawRoundedRectangle(bounds, r, 1.5f);
        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRoundedRectangle(bounds, r);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        bool isOn = button.getToggleState();
        auto bounds = button.getLocalBounds();
        float fontSize = juce::jmin(13.0f, (float)bounds.getHeight() * 0.65f);
        g.setFont(juce::Font(fontSize, juce::Font::bold));
        juce::Colour tc = isOn ? juce::Colours::white : juce::Colours::white.withAlpha(0.30f);
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    g.drawFittedText(button.getButtonText(), bounds.translated(dx, dy),
                                     juce::Justification::centred, 1);
        g.setColour(tc);
        g.drawFittedText(button.getButtonText(), bounds, juce::Justification::centred, 1);
    }
};

class FaderIconLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        bool isOn = button.getToggleState();
        const float r = 4.0f;

        g.setColour(isOn ? juce::Colour::fromRGB(15, 55, 60)
                         : juce::Colour::fromRGB(10, 22, 26));
        g.fillRoundedRectangle(bounds, r);

        g.setColour(isOn ? juce::Colour::fromRGB(30, 180, 200)
                         : juce::Colour::fromRGB(30, 180, 200).withAlpha(0.22f));
        g.drawRoundedRectangle(bounds, r, 1.0f);

        // 5 fader tracks + handles
        juce::Colour iconCol = isOn ? juce::Colour::fromRGB(40, 210, 230)
                                    : juce::Colour::fromRGB(40, 210, 230).withAlpha(0.22f);
        g.setColour(iconCol);

        float ix = bounds.getX() + bounds.getWidth() * 0.09f;
        float iw = bounds.getWidth() * 0.82f;
        float iy = bounds.getY() + bounds.getHeight() * 0.12f;
        float ih = bounds.getHeight() * 0.76f;

        const float pos[5] = { 0.35f, 0.65f, 0.2f, 0.55f, 0.45f };
        float fw = iw / 5.0f;
        for (int i = 0; i < 5; ++i)
        {
            float cx = ix + fw * (i + 0.5f);
            g.drawLine(cx, iy, cx, iy + ih, 1.2f);
            float hy = iy + ih * pos[i];
            float hw = fw * 0.62f;
            float hh = juce::jmax(3.0f, ih * 0.18f);
            g.fillRect(cx - hw * 0.5f, hy - hh * 0.5f, hw, hh);
        }

        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRoundedRectangle(bounds, r);
        }
    }
};

class NinjamVst3AudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::Timer,
                                       private juce::OSCReceiver,
                                       private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>,
                                       private juce::MidiInputCallback
{
public:
    NinjamVst3AudioProcessorEditor (NinjamVst3AudioProcessor&);
    ~NinjamVst3AudioProcessorEditor() override;
    
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void parentHierarchyChanged() override;
    void mouseDown(const juce::MouseEvent& event) override;
    bool shouldDeferHeavyUiWork() const;
    
    juce::Image backgroundImage;
    juce::Image radioKnobImage;
    juce::Image faderKnobImage;
    juce::Array<juce::File> textureFiles;
#if JUCE_WINDOWS
    std::unique_ptr<WinVideoReader> videoFrameReader;
#endif
    juce::String knobColourPreset { "grey" };
    juce::String faderColourPreset { "grey" };
    juce::Colour knobThemeColour { juce::Colours::grey };
    juce::Colour faderThemeColour { juce::Colour(0xff666666) };
    juce::Colour metronomeThemeColour { juce::Colour::fromRGB(80, 185, 255) };
    juce::Colour windowThemeColour    { juce::Colour(0x00000000) };  // transparent = no override
    juce::Colour buttonThemeColour    { juce::Colour(0x00000000) };  // transparent = no override
    juce::Colour menuBarThemeColour   { juce::Colour(0x00000000) };  // transparent = no override
    CustomKnobLookAndFeel customKnobLookAndFeel;
    FaderIconLookAndFeel faderIconLookAndFeel;
    MetronomeButtonLookAndFeel metronomeBtnLAF;
    SyncIconLookAndFeel syncIconLAF;
    ATButtonLookAndFeel atBtnLAF;
    ATButtonLookAndFeel chatBtnLAF;
    OutlinedLabelLookAndFeel outlinedLabelLAF;
    
private:
    NinjamVst3AudioProcessor& audioProcessor;
    IntervalDisplayComponent intervalDisplay;
    juce::TooltipWindow tooltipWindow{ this, 600 };
    
    // UI components
    juce::Label statusLabel;
    
    // Login
    juce::Label serverLabel{ "Server", "Server:" };
    juce::TextEditor serverField;
    LeftClickOnlyTextButton serverListButton;
    juce::Label userLabel{ "User", "User:" };
    juce::TextEditor userField;
    LeftClickOnlyToggleButton anonymousButton{ "Anonymous" };
    juce::Label passLabel{ "Pass", "Pass:" };
    juce::TextEditor passField;
    LeftClickOnlyTextButton connectButton;
    
    // Controls
    LeftClickOnlyTextButton transmitButton{ "Transmit" };
    LeftClickOnlyTextButton localMonitorButton{ "Monitor Local" };
    LeftClickOnlyTextButton voiceChatButton{ "Voice Chat" };
    juce::ComboBox bitrateSelector;
    juce::ComboBox midiRelayTargetSelector;
    LeftClickOnlyTextButton layoutButton{ "" };
    LeftClickOnlyTextButton opusSyncToggle{ "HD" };
    juce::Label metronomeLabel{ "Metro", "Metronome:" };
    LeftClickOnlySlider metronomeSlider;
    LeftClickOnlyTextButton metronomeMuteButton{ "" };
    LeftClickOnlyTextButton autoLevelButton{ "Auto Level" };
    LeftClickOnlyTextButton syncButton{ "" };
    LeftClickOnlyTextButton fxButton{ "FX" };
    LeftClickOnlyTextButton optionsButton{ "Options" };
    juce::Label tempoLabel;
    juce::ComboBox backgroundSelector{ "Background" };
    LeftClickOnlyToggleButton videoBgToggle{ "Video BG" };
    LeftClickOnlyTextButton videoButton{ "Video Room" };
    LeftClickOnlyTextButton chatButton{ "Chat" };
    
    // Chat
    juce::TextEditor chatDisplay;
    juce::TextEditor chatInput;
    LeftClickOnlyTextButton sendButton{ "Send" };
    LeftClickOnlyTextButton atButton{ "AT" };
    LeftClickOnlyTextButton chatPopoutButton{ "Popout" };
    
    // Users
    juce::Label usersLabel{ "Users", "Connected Users:" };
    LeftClickOnlyToggleButton spreadOutputsButton{ "Spread Outputs" };
    UserListComponent userList;

    FaderLookAndFeel mixerFaderLookAndFeel;
    juce::Label localFaderLabel{ "Local", "Local" };
    LeftClickOnlyTextButton addLocalChannelButton{ "+" };
    LeftClickOnlyTextButton removeLocalChannelButton{ "-" };
    std::array<NonlinearFaderSlider, NinjamVst3AudioProcessor::maxLocalChannels> localFaders;
    std::array<MasterPeakMeter, NinjamVst3AudioProcessor::maxLocalChannels> localPeakMeters;
    std::array<juce::ComboBox, NinjamVst3AudioProcessor::maxLocalChannels> localInputModeSelectors;
    std::array<juce::ComboBox, NinjamVst3AudioProcessor::maxLocalChannels> localInputSelectors;
    std::array<LeftClickOnlySlider, NinjamVst3AudioProcessor::maxLocalChannels> localReverbSendKnobs;
    std::array<LeftClickOnlySlider, NinjamVst3AudioProcessor::maxLocalChannels> localDelaySendKnobs;
    std::array<juce::Label, NinjamVst3AudioProcessor::maxLocalChannels> localReverbSendLabels;
    std::array<juce::Label, NinjamVst3AudioProcessor::maxLocalChannels> localDelaySendLabels;
    juce::Label masterFaderLabel{ "Master", "Master" };
    NonlinearFaderSlider masterFader;
    MasterPeakMeter masterPeakMeter;
    std::array<juce::Label, NinjamVst3AudioProcessor::maxLocalChannels> localDbLabels;
    std::array<juce::Label, NinjamVst3AudioProcessor::maxLocalChannels> localChannelNameLabels; // editable channel name
    juce::Label masterDbLabel;
    LeftClickOnlyTextButton limiterButton{ "Limiter" };
    juce::Label limiterReleaseLabel{ "Release", "Release" };
    class LimiterThresholdLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              const juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            juce::Rectangle<int> bounds(x, y, width, height);
            auto track = bounds.reduced(width / 3, 6);
            g.setColour(juce::Colours::black);
            g.fillRect(track);
            g.setColour(juce::Colours::darkgrey.brighter(0.2f));
            g.drawRect(track);

            int handleHeight = 10;
            int handleWidth = track.getWidth() + 4;
            int clampedY = juce::jlimit(track.getY() + handleHeight / 2,
                                        track.getBottom() - handleHeight / 2,
                                        (int)sliderPos);
            juce::Rectangle<int> handle(track.getCentreX() - handleWidth / 2,
                                        clampedY - handleHeight / 2,
                                        handleWidth,
                                        handleHeight);

            g.setColour(juce::Colours::lightblue);
            g.fillRect(handle);
            g.setColour(juce::Colours::black);
            g.drawRect(handle);
        }
    } limiterThresholdLookAndFeel;

    LeftClickOnlySlider limiterThresholdSlider;
    LeftClickOnlySlider limiterReleaseSlider;
    juce::Label reverbRoomLabel{ "Reverb", "Reverb" };
    LeftClickOnlySlider reverbRoomSlider;
    juce::Label delayTimeLabel{ "Delay", "Delay" };
    LeftClickOnlySlider delayTimeSlider;
    juce::ComboBox delayDivisionSelector;
    LeftClickOnlyToggleButton delayPingPongButton{ "PingPong" };
    
    void connectClicked();
    void sendClicked();
    void transmitToggled();
    void layoutToggled();
    void metronomeChanged();
    void anonymousToggled();
    void atToggled();
    void syncToggled();
    void chatToggled();
    void chatPopoutClicked();
    void videoClicked();

    void serverListClicked();
    void updateAutoLevelButtonColor();
    void updateChatButtonColor();
    void updateTransmitButtonColor();
    void updateMonitorButtonColor();
    void updateLimiterButtonColor();
    void updateVoiceChatButtonColor();
    void updateLayoutButtonColor();
    void updateMetronomeButtonColor();
    void updateSyncButtonColor();
    void updateFxButtonLabel();
    void showFxMenu();
    void showOptionsMenu();
    void showSettingsCallout(std::unique_ptr<juce::Component> content, juce::Component& anchorComponent);
    void showReverbSettingsPopup();
    void showDelaySettingsPopup();
    void updateFxControlsVisibility();
    void refreshLocalInputSelectors();
    void refreshMidiRelayTargetSelector();
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void applyOscMappings();
    void applyRemoteMidiRelaySelection(int channel, int inputIndex);
    void refreshLocalInputSelector(int channel);
    void showMidiOptionsPopup();
    void refreshExternalMidiInputDevices();
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void syncLearnMappingsToProcessor();
    void loadLearnMappingsFromProcessor();
    void saveLearnMappingsToDisk();
    void loadLearnMappingsFromDisk();
    void clearLearnMappings();
    bool isSidechainInputActive() const;
    bool isAbletonLiveHost() const;
    void setAbletonWindowSizePreset(int presetIndex);
    void updateHostResizeModeForConnectionStatus(int status);
    void loadControlImages(const juce::File& themeDir);
    void applyThemeColours();
    void registerMidiLearnTarget(juce::Component& component, const juce::String& targetId, bool isToggle);
    void syncUserStripMidiTargets();
    void showMidiLearnMenuForComponent(juce::Component& component, juce::Point<int> screenPos);
    void applyMidiMappings();

    struct MidiLearnTarget
    {
        juce::String id;
        juce::Component* component = nullptr;
        bool isToggle = false;
    };

    struct MidiSourceMapping
    {
        bool isController = true;
        int midiChannel = 1;
        int number = 0;
        int lastBinaryState = -1;
    };

    struct OscSourceMapping
    {
        juce::String address;
        int lastBinaryState = -1;
    };

    struct PendingOscEvent
    {
        juce::String address;
        float normalized = 0.0f;
        bool binaryOn = false;
    };
    
    int lastChatSize = 0;

    std::unique_ptr<juce::DocumentWindow> serverListWindow;
    std::unique_ptr<juce::DocumentWindow> chatWindow;

    bool autoLevelEnabled = false;
    bool chatPoppedOut = false;
    bool pendingDeferredResizeLayout = false;
    bool applyingDeferredResizeLayout = false;
    bool hostResizeLockedForConnection = false;
    int abletonWindowSizePreset = 1;
    double lastResizeEventMs = 0.0;
    double suppressHeavyUiUntilMs = 0.0;
    int heavyUiTickCounter = 0;
    float voiceChatGlowPhase = 0.0f;
    float storedMetronomeVolume = 0.5f;
    std::map<int, float> autoLevelCurrentGains;
    std::map<int, float> autoLevelPeakLevels;
    std::map<int, int> autoLevelChannelActiveTicks;
    std::map<int, int> autoLevelMeasureTicks;
    std::map<int, int> autoLevelOverTargetTicks;
    std::map<juce::Component*, MidiLearnTarget> midiTargetsByComponent;
    std::map<juce::String, MidiLearnTarget> midiTargetsById;
    std::map<juce::String, MidiSourceMapping> midiSourceByTargetId;
    std::map<juce::String, OscSourceMapping> oscSourceByTargetId;
    juce::String midiLearnArmedTargetId;
    juce::String oscLearnArmedTargetId;
    juce::SpinLock oscEventQueueLock;
    std::vector<PendingOscEvent> pendingOscEvents;
    std::map<int, juce::String> midiRelayTargetByMenuId;
    std::unique_ptr<juce::MidiInput> midiLearnInputDevice;
    std::unique_ptr<juce::MidiInput> midiRelayInputDevice;
    juce::String openedMidiLearnInputDeviceId;
    juce::String openedMidiRelayInputDeviceId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NinjamVst3AudioProcessorEditor)
};
