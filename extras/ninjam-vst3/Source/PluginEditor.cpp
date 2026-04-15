#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String normaliseColourPresetName(const juce::String& name);
static juce::Colour colourFromPresetName(const juce::String& preset, const juce::Colour& fallback);

#if JUCE_WINDOWS
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

/** Decodes video frames on a background thread using Windows Media Foundation.
    The main thread calls getLatestFrame() — it returns instantly (no blocking). */
struct WinVideoReader : public juce::Thread
{
    WinVideoReader() : juce::Thread ("BgVideoDecoder") {}

    ~WinVideoReader() override
    {
        signalThreadShouldExit();
        stopThread (3000);
        if (reader    != nullptr) { reader->Release();  reader    = nullptr; }
        if (mfStarted)           { MFShutdown();        mfStarted = false; }
    }

    bool open (const juce::File& file)
    {
        if (FAILED (MFStartup (MF_VERSION))) return false;
        mfStarted = true;

        IMFAttributes* attrs = nullptr;
        MFCreateAttributes (&attrs, 1);
        attrs->SetUINT32 (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        HRESULT hr = MFCreateSourceReaderFromURL (
            file.getFullPathName().toWideCharPointer(), attrs, &reader);
        attrs->Release();
        if (FAILED (hr) || reader == nullptr) return false;

        IMFMediaType* type = nullptr;
        MFCreateMediaType (&type);
        type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID (MF_MT_SUBTYPE,    MFVideoFormat_RGB32);
        reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
        type->Release();

        IMFMediaType* outType = nullptr;
        if (SUCCEEDED (reader->GetCurrentMediaType (
                (DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, &outType)))
        {
            UINT32 w = 0, h = 0;
            MFGetAttributeSize (outType, MF_MT_FRAME_SIZE, &w, &h);
            frameWidth  = (int) w;
            frameHeight = (int) h;

            UINT32 num = 0, den = 0;
            MFGetAttributeRatio (outType, MF_MT_FRAME_RATE, &num, &den);
            if (num > 0 && den > 0)
                framePeriodMs = juce::jlimit (10, 200, (int) (1000.0 * den / num));

            outType->Release();
        }

        if (frameWidth <= 0 || frameHeight <= 0) return false;

        startThread (juce::Thread::Priority::low);
        return true;
    }

    /** Called from the message thread. Returns a new frame image if one is ready,
        or an invalid Image if nothing new has been decoded since last call. */
    juce::Image getLatestFrame()
    {
        juce::ScopedLock sl (frameLock);
        auto f = pendingFrame;   // ref-counted copy — cheap
        pendingFrame = {};
        return f;
    }

    //==============================================================================
    void run() override
    {
        CoInitializeEx (nullptr, COINIT_MULTITHREADED);

        while (!threadShouldExit())
        {
            if (eof)
            {
                seekToStart();
                continue;   // go straight back to decode after looping
            }

            auto img = decodeNextFrame();
            if (img.isValid())
            {
                juce::ScopedLock sl (frameLock);
                pendingFrame = std::move (img);
            }

            // Sleep one frame period between decodes; wakes early on exit signal
            wait (framePeriodMs);
        }

        CoUninitialize();
    }

private:
    IMFSourceReader* reader = nullptr;
    bool mfStarted          = false;
    int  frameWidth         = 0;
    int  frameHeight        = 0;
    int  framePeriodMs      = 33;   // ~30 fps default
    bool eof                = false;

    juce::CriticalSection frameLock;
    juce::Image           pendingFrame;

    void seekToStart()
    {
        if (reader == nullptr) return;
        PROPVARIANT pv;
        PropVariantInit (&pv);
        pv.vt            = VT_I8;
        pv.hVal.QuadPart = 0;
        reader->SetCurrentPosition (GUID_NULL, pv);
        PropVariantClear (&pv);
        eof = false;
    }

    juce::Image decodeNextFrame()
    {
        if (reader == nullptr) return {};

        DWORD    streamIdx = 0, flags = 0;
        LONGLONG ts        = 0;
        IMFSample* sample  = nullptr;

        HRESULT hr = reader->ReadSample (
            (DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0, &streamIdx, &flags, &ts, &sample);

        if (FAILED (hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
        {
            if (sample != nullptr) sample->Release();
            eof = true;
            return {};
        }
        if (sample == nullptr) return {};

        IMFMediaBuffer* buf = nullptr;
        sample->ConvertToContiguousBuffer (&buf);
        sample->Release();
        if (buf == nullptr) return {};

        BYTE* data   = nullptr;
        DWORD maxLen = 0, curLen = 0;
        buf->Lock (&data, &maxLen, &curLen);

        juce::Image img (juce::Image::ARGB, frameWidth, frameHeight, false);
        {
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            const size_t srcRowBytes = (size_t) frameWidth * 4;
            for (int y = 0; y < frameHeight; ++y)
            {
                auto* src = reinterpret_cast<const uint32_t*> (data + y * srcRowBytes);
                auto* dst = reinterpret_cast<uint32_t*> (bd.getLinePointer (y));
                for (int x = 0; x < frameWidth; ++x)
                    dst[x] = src[x] | 0xFF000000u;
            }
        }

        buf->Unlock();
        buf->Release();
        return img;
    }
};
#endif  // JUCE_WINDOWS

namespace
{
class NoArrowCallOutLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path&, juce::Image&) override
    {
        auto bounds = box.getLocalBounds().toFloat().reduced(1.0f);
        auto background = box.findColour(juce::ResizableWindow::backgroundColourId).withAlpha(0.97f);
        g.setColour(background);
        g.fillRoundedRectangle(bounds, 10.0f);
        g.setColour(juce::Colours::lightblue.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
    }
};

static NoArrowCallOutLookAndFeel noArrowCallOutLookAndFeel;

class ReverbSettingsPopupComponent : public juce::Component
{
public:
    explicit ReverbSettingsPopupComponent(NinjamVst3AudioProcessor& p)
        : processor(p)
    {
        addAndMakeVisible(roomSizeLabel);
        roomSizeLabel.setText("Room Size", juce::dontSendNotification);
        addAndMakeVisible(roomSizeSlider);
        roomSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        roomSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        roomSizeSlider.setRange(0.0, 1.0, 0.01);
        roomSizeSlider.setValue(processor.getFxReverbRoomSize(), juce::dontSendNotification);
        roomSizeSlider.onValueChange = [this] { processor.setFxReverbRoomSize((float)roomSizeSlider.getValue()); };

        addAndMakeVisible(dampingLabel);
        dampingLabel.setText("Dampening", juce::dontSendNotification);
        addAndMakeVisible(dampingSlider);
        dampingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        dampingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        dampingSlider.setRange(0.0, 1.0, 0.01);
        dampingSlider.setValue(processor.getFxReverbDamping(), juce::dontSendNotification);
        dampingSlider.onValueChange = [this] { processor.setFxReverbDamping((float)dampingSlider.getValue()); };

        addAndMakeVisible(wetDryLabel);
        wetDryLabel.setText("Wet/Dry", juce::dontSendNotification);
        addAndMakeVisible(wetDrySlider);
        wetDrySlider.setSliderStyle(juce::Slider::LinearHorizontal);
        wetDrySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        wetDrySlider.setRange(0.0, 1.0, 0.01);
        wetDrySlider.setValue(processor.getFxReverbWetDryMix(), juce::dontSendNotification);
        wetDrySlider.onValueChange = [this] { processor.setFxReverbWetDryMix((float)wetDrySlider.getValue()); };

        addAndMakeVisible(earlyLabel);
        earlyLabel.setText("Early Reflections", juce::dontSendNotification);
        addAndMakeVisible(earlySlider);
        earlySlider.setSliderStyle(juce::Slider::LinearHorizontal);
        earlySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        earlySlider.setRange(0.0, 1.0, 0.01);
        earlySlider.setValue(processor.getFxReverbEarlyReflections(), juce::dontSendNotification);
        earlySlider.onValueChange = [this] { processor.setFxReverbEarlyReflections((float)earlySlider.getValue()); };

        addAndMakeVisible(tailLabel);
        tailLabel.setText("Tail", juce::dontSendNotification);
        addAndMakeVisible(tailSlider);
        tailSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        tailSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        tailSlider.setRange(0.0, 1.0, 0.01);
        tailSlider.setValue(processor.getFxReverbTail(), juce::dontSendNotification);
        tailSlider.onValueChange = [this] { processor.setFxReverbTail((float)tailSlider.getValue()); };

        setSize(340, 180);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        layoutRow(area, roomSizeLabel, roomSizeSlider);
        layoutRow(area, earlyLabel, earlySlider);
        layoutRow(area, tailLabel, tailSlider);
        layoutRow(area, dampingLabel, dampingSlider);
        layoutRow(area, wetDryLabel, wetDrySlider);
    }

private:
    void layoutRow(juce::Rectangle<int>& area, juce::Label& label, juce::Slider& slider)
    {
        auto row = area.removeFromTop(32);
        label.setBounds(row.removeFromLeft(130));
        slider.setBounds(row);
    }

    NinjamVst3AudioProcessor& processor;
    juce::Label roomSizeLabel;
    juce::Label dampingLabel;
    juce::Label wetDryLabel;
    juce::Label earlyLabel;
    juce::Label tailLabel;
    juce::Slider roomSizeSlider;
    juce::Slider dampingSlider;
    juce::Slider wetDrySlider;
    juce::Slider earlySlider;
    juce::Slider tailSlider;
};

class DelaySettingsPopupComponent : public juce::Component
{
public:
    explicit DelaySettingsPopupComponent(NinjamVst3AudioProcessor& p)
        : processor(p)
    {
        addAndMakeVisible(modeLabel);
        modeLabel.setText("Time Mode", juce::dontSendNotification);
        addAndMakeVisible(modeSelector);
        modeSelector.addItem("Milliseconds", 1);
        modeSelector.addItem("Sync Host", 2);
        modeSelector.setSelectedId(processor.isFxDelaySyncToHost() ? 2 : 1, juce::dontSendNotification);
        modeSelector.onChange = [this]
        {
            processor.setFxDelaySyncToHost(modeSelector.getSelectedId() == 2);
            updateEnabledState();
        };

        addAndMakeVisible(timeMsLabel);
        timeMsLabel.setText("Delay Time (ms)", juce::dontSendNotification);
        addAndMakeVisible(timeMsSlider);
        timeMsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        timeMsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        timeMsSlider.setRange(20.0, 2000.0, 1.0);
        timeMsSlider.setValue(processor.getFxDelayTimeMs(), juce::dontSendNotification);
        timeMsSlider.onValueChange = [this] { processor.setFxDelayTimeMs((float)timeMsSlider.getValue()); };

        addAndMakeVisible(syncLabel);
        syncLabel.setText("Sync Division", juce::dontSendNotification);
        addAndMakeVisible(syncSelector);
        syncSelector.addItem("1/16", 16);
        syncSelector.addItem("1/8", 8);
        syncSelector.addItem("1/1", 1);
        syncSelector.setSelectedId(processor.getFxDelayDivision(), juce::dontSendNotification);
        syncSelector.onChange = [this]
        {
            int division = syncSelector.getSelectedId();
            if (division <= 0)
                division = 8;
            processor.setFxDelayDivision(division);
        };

        addAndMakeVisible(pingPongLabel);
        pingPongLabel.setText("Ping Pong", juce::dontSendNotification);
        addAndMakeVisible(pingPongToggle);
        pingPongToggle.setClickingTogglesState(true);
        pingPongToggle.setToggleState(processor.isFxDelayPingPong(), juce::dontSendNotification);
        pingPongToggle.onClick = [this] { processor.setFxDelayPingPong(pingPongToggle.getToggleState()); };

        addAndMakeVisible(wetDryLabel);
        wetDryLabel.setText("Wet/Dry", juce::dontSendNotification);
        addAndMakeVisible(wetDrySlider);
        wetDrySlider.setSliderStyle(juce::Slider::LinearHorizontal);
        wetDrySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        wetDrySlider.setRange(0.0, 1.0, 0.01);
        wetDrySlider.setValue(processor.getFxDelayWetDryMix(), juce::dontSendNotification);
        wetDrySlider.onValueChange = [this] { processor.setFxDelayWetDryMix((float)wetDrySlider.getValue()); };

        addAndMakeVisible(feedbackLabel);
        feedbackLabel.setText("Feedback", juce::dontSendNotification);
        addAndMakeVisible(feedbackSlider);
        feedbackSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        feedbackSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        feedbackSlider.setRange(0.0, 0.95, 0.01);
        feedbackSlider.setValue(processor.getFxDelayFeedback(), juce::dontSendNotification);
        feedbackSlider.onValueChange = [this] { processor.setFxDelayFeedback((float)feedbackSlider.getValue()); };

        updateEnabledState();
        setSize(360, 220);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        layoutRow(area, modeLabel, modeSelector);
        layoutRow(area, timeMsLabel, timeMsSlider);
        layoutRow(area, syncLabel, syncSelector);
        layoutRow(area, pingPongLabel, pingPongToggle);
        layoutRow(area, wetDryLabel, wetDrySlider);
        layoutRow(area, feedbackLabel, feedbackSlider);
    }

private:
    template <typename ControlType>
    void layoutRow(juce::Rectangle<int>& area, juce::Label& label, ControlType& control)
    {
        auto row = area.removeFromTop(34);
        label.setBounds(row.removeFromLeft(130));
        control.setBounds(row);
    }

    void updateEnabledState()
    {
        const bool syncEnabled = modeSelector.getSelectedId() == 2;
        timeMsSlider.setEnabled(!syncEnabled);
        syncSelector.setEnabled(syncEnabled);
    }

    NinjamVst3AudioProcessor& processor;
    juce::Label modeLabel;
    juce::ComboBox modeSelector;
    juce::Label timeMsLabel;
    juce::Slider timeMsSlider;
    juce::Label syncLabel;
    juce::ComboBox syncSelector;
    juce::Label pingPongLabel;
    juce::ToggleButton pingPongToggle;
    juce::Label wetDryLabel;
    juce::Slider wetDrySlider;
    juce::Label feedbackLabel;
    juce::Slider feedbackSlider;
};

class MidiOptionsPopupComponent : public juce::Component
{
public:
    MidiOptionsPopupComponent(NinjamVst3AudioProcessor& p, std::function<void()> onChangedCallback)
        : processor(p), onChanged(std::move(onChangedCallback))
    {
        addAndMakeVisible(learnDeviceLabel);
        learnDeviceLabel.setText("Midi Learn Device", juce::dontSendNotification);
        addAndMakeVisible(learnDeviceSelector);
        populateSelector(learnDeviceSelector, learnDeviceByMenuId, processor.getMidiLearnInputDeviceId());
        learnDeviceSelector.onChange = [this]
        {
            const int selected = learnDeviceSelector.getSelectedId();
            auto it = learnDeviceByMenuId.find(selected);
            processor.setMidiLearnInputDeviceId(it != learnDeviceByMenuId.end() ? it->second : juce::String());
            if (onChanged)
                onChanged();
        };

        addAndMakeVisible(relayDeviceLabel);
        relayDeviceLabel.setText("Midi Relay Device", juce::dontSendNotification);
        addAndMakeVisible(relayDeviceSelector);
        populateSelector(relayDeviceSelector, relayDeviceByMenuId, processor.getMidiRelayInputDeviceId());
        relayDeviceSelector.onChange = [this]
        {
            const int selected = relayDeviceSelector.getSelectedId();
            auto it = relayDeviceByMenuId.find(selected);
            processor.setMidiRelayInputDeviceId(it != relayDeviceByMenuId.end() ? it->second : juce::String());
            if (onChanged)
                onChanged();
        };

        setSize(360, 104);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        layoutRow(area, learnDeviceLabel, learnDeviceSelector);
        layoutRow(area, relayDeviceLabel, relayDeviceSelector);
    }

private:
    template <typename ControlType>
    void layoutRow(juce::Rectangle<int>& area, juce::Label& label, ControlType& control)
    {
        auto row = area.removeFromTop(42);
        label.setBounds(row.removeFromLeft(140));
        control.setBounds(row.removeFromTop(28));
    }

    static void populateSelector(juce::ComboBox& selector,
                                 std::map<int, juce::String>& idByMenuId,
                                 const juce::String& selectedDeviceId)
    {
        selector.clear(juce::dontSendNotification);
        idByMenuId.clear();
        int menuId = 1;
        selector.addItem("Host MIDI / Any", menuId);
        idByMenuId[menuId] = {};
        int selectedMenuId = selectedDeviceId.isEmpty() ? menuId : 0;
        ++menuId;

        const auto devices = juce::MidiInput::getAvailableDevices();
        for (const auto& device : devices)
        {
            selector.addItem(device.name, menuId);
            idByMenuId[menuId] = device.identifier;
            if (device.identifier == selectedDeviceId)
                selectedMenuId = menuId;
            ++menuId;
        }

        if (selectedMenuId == 0)
            selectedMenuId = 1;
        selector.setSelectedId(selectedMenuId, juce::dontSendNotification);
    }

    NinjamVst3AudioProcessor& processor;
    std::function<void()> onChanged;
    juce::Label learnDeviceLabel;
    juce::ComboBox learnDeviceSelector;
    juce::Label relayDeviceLabel;
    juce::ComboBox relayDeviceSelector;
    std::map<int, juce::String> learnDeviceByMenuId;
    std::map<int, juce::String> relayDeviceByMenuId;
};
}

void FaderLookAndFeel::drawLinearSliderBackground(juce::Graphics& g, int x, int y, int width, int height,
                                                  float sliderPos, float minSliderPos, float maxSliderPos,
                                                  const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::Rectangle<int> bounds(x, y, width, height);
    if (style == juce::Slider::LinearVertical)
    {
        int trackWidth = 4;
        juce::Rectangle<int> track(bounds.getCentreX() - trackWidth / 2,
                                   bounds.getY() + 4,
                                   trackWidth,
                                   bounds.getHeight() - 8);

        juce::ColourGradient grad(juce::Colours::black.withAlpha(0.9f), (float)track.getCentreX(), (float)track.getY(),
                                  juce::Colours::darkgrey.darker(), (float)track.getCentreX(), (float)track.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRect(track);

        g.setColour(juce::Colours::black);
        g.drawRect(track);

        int tickX = track.getRight() + 6;

        const int numTicksBelowZero = 3;
        float zeroProp = slider.valueToProportionOfLength(1.0);
        zeroProp = juce::jlimit(0.0f, 1.0f, zeroProp);

        for (int i = 0; i <= numTicksBelowZero + 1; ++i)
        {
            float prop = zeroProp * (float)i / (float)numTicksBelowZero;
            if (i > numTicksBelowZero) prop = 1.0f;
            double value = slider.proportionOfLengthToValue(prop);
            float gain = (float)value;
            float clampedGain = juce::jlimit(1.0e-6f, 2.0f, gain);
            float db = 20.0f * std::log10(clampedGain);

            int yPos = track.getY() + (int)((1.0f - prop) * (float)track.getHeight());

            float alpha = 0.7f;
            if (i == numTicksBelowZero) alpha = 0.95f;

            g.setColour(juce::Colours::lightgrey.withAlpha(alpha));
            g.drawLine((float)(track.getX() - 6), (float)yPos,
                       (float)(tickX + 4), (float)yPos);

            juce::String label;
            if (i == numTicksBelowZero)
                label = "0 dB";
            else if (i > numTicksBelowZero)
                label = "+6 dB";
            else
                label = juce::String((int)std::round(db)) + " dB";

            g.setFont(9.0f);
            g.drawText(label, tickX + 4, yPos - 7, 40, 14,
                       juce::Justification::centredLeft, false);
        }
    }
    else if (style == juce::Slider::LinearHorizontal)
    {
        int trackHeight = 4;
        juce::Rectangle<int> track(bounds.getX() + 4,
                                   bounds.getCentreY() - trackHeight / 2,
                                   bounds.getWidth() - 8,
                                   trackHeight);

        juce::ColourGradient grad(juce::Colours::black.withAlpha(0.9f), (float)track.getX(), (float)track.getCentreY(),
                                  juce::Colours::darkgrey.darker(), (float)track.getRight(), (float)track.getCentreY(), false);
        g.setGradientFill(grad);
        g.fillRect(track);

        g.setColour(juce::Colours::black);
        g.drawRect(track);

        const int numTicksBelowZero = 3;
        float zeroProp = slider.valueToProportionOfLength(1.0);
        zeroProp = juce::jlimit(0.0f, 1.0f, zeroProp);

        for (int i = 0; i <= numTicksBelowZero + 1; ++i)
        {
            float prop = zeroProp * (float)i / (float)numTicksBelowZero;
            if (i > numTicksBelowZero) prop = 1.0f;
            double value = slider.proportionOfLengthToValue(prop);
            float gain = (float)value;
            float clampedGain = juce::jlimit(1.0e-6f, 2.0f, gain);
            float db = 20.0f * std::log10(clampedGain);

            int xPos = track.getX() + (int)(prop * (float)track.getWidth());

            float alpha = 0.7f;
            if (i == numTicksBelowZero) alpha = 0.95f;

            g.setColour(juce::Colours::lightgrey.withAlpha(alpha));
            g.drawLine((float)xPos, (float)(track.getY() - 6),
                       (float)xPos, (float)(track.getBottom() + 6));

            juce::String label;
            if (i == numTicksBelowZero)
                label = "0 dB";
            else if (i > numTicksBelowZero)
                label = "+6 dB";
            else
                label = juce::String((int)std::round(db)) + " dB";

            g.setFont(10.0f);
            g.drawText(label,
                       xPos - 20,
                       track.getBottom() + 8,
                       40,
                       14,
                       juce::Justification::centred,
                       false);
        }
    }
    else
    {
        juce::LookAndFeel_V4::drawLinearSliderBackground(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
}

void FaderLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float minSliderPos, float maxSliderPos,
                                        const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    // Walk parent hierarchy to find the editor (sliders may be inside sub-components)
    NinjamVst3AudioProcessorEditor* editor = nullptr;
    for (auto* p = slider.getParentComponent(); p != nullptr && editor == nullptr; p = p->getParentComponent())
        editor = dynamic_cast<NinjamVst3AudioProcessorEditor*>(p);

    if (editor != nullptr && editor->faderKnobImage.isValid())
    {
        drawLinearSliderBackground(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        bool isVert = (style == juce::Slider::LinearVertical);
        float thumbW = isVert ? (float)width  * 0.95f : 40.0f;
        float thumbH = isVert ? 42.0f : (float)height * 0.95f;
        float thumbX = isVert ? (float)x + (float)width  * 0.025f : sliderPos - thumbW * 0.5f;
        float thumbY = isVert ? sliderPos - thumbH * 0.5f         : (float)y + (float)height * 0.025f;
        // Clamp so thumb never clips outside the slider bounds
        thumbX = juce::jlimit((float)x,              (float)(x + width)  - thumbW, thumbX);
        thumbY = juce::jlimit((float)y,              (float)(y + height) - thumbH, thumbY);
        g.drawImageWithin(editor->faderKnobImage, (int)thumbX, (int)thumbY, (int)thumbW, (int)thumbH,
                          juce::RectanglePlacement::centred);
        return;
    }

    if (style == juce::Slider::LinearVertical)
    {
        drawLinearSliderBackground(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);

        juce::Rectangle<int> bounds(x, y, width, height);
        int trackWidth = 4;
        juce::Rectangle<int> track(bounds.getCentreX() - trackWidth / 2,
                                   bounds.getY() + 4,
                                   trackWidth,
                                   bounds.getHeight() - 8);

        int thumbHeight = juce::jmin(52, track.getHeight() / 2);
        int thumbWidth = 30;
        int thumbY = juce::jlimit(bounds.getY(), bounds.getBottom() - thumbHeight,
                                  (int)sliderPos - thumbHeight / 2);
        juce::Rectangle<int> thumb(track.getCentreX() - thumbWidth / 2, thumbY, thumbWidth, thumbHeight);

        const juce::Colour base = editor != nullptr ? editor->faderThemeColour : juce::Colour(0xff666666);
        juce::ColourGradient grad(base.brighter(0.5f), (float)thumb.getX(), (float)thumb.getY(),
                                  base.darker(0.4f), (float)thumb.getRight(), (float)thumb.getBottom(), false);
        if (editor != nullptr && normaliseColourPresetName(editor->faderColourPreset).startsWith("multi"))
            grad.addColour(0.4, juce::Colour::fromHSV((float)slider.getValue() * 0.8f, 0.8f, 1.0f, 1.0f));
        else
            grad.addColour(0.4, base.brighter(0.2f));
        g.setGradientFill(grad);
        g.fillRect(thumb);

        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawRect(thumb);

        g.setColour(juce::Colours::white.withAlpha(0.4f));
        int innerY = thumb.getY() + 4;
        for (int i = 0; i < 4; ++i)
        {
            g.drawLine((float)(thumb.getX() + 2), (float)innerY, (float)(thumb.getRight() - 2), (float)innerY);
            innerY += 4;
        }

        double v = slider.getValue();
        double db = (v <= 1.0e-6) ? -60.0 : 20.0 * std::log10(v);
        int dbInt = (int)std::round(db);

        juce::Rectangle<int> box(thumb.getX() + 3, thumb.getY() + 6, thumb.getWidth() - 6, 14);
        g.setColour(juce::Colours::black);
        g.fillRect(box);
        g.setColour(juce::Colours::white.withAlpha(0.9f));

        juce::String text;
        if (v <= 1.0e-6)
            text = "-inf";
        else if (dbInt > 0)
            text = "+" + juce::String(dbInt) + " dB";
        else
            text = juce::String(dbInt) + " dB";

        g.setFont(10.0f);
        g.drawText(text, box, juce::Justification::centred, false);
    }
    else if (style == juce::Slider::LinearHorizontal)
    {
        drawLinearSliderBackground(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);

        juce::Rectangle<int> bounds(x, y, width, height);
        int trackHeight = 4;
        juce::Rectangle<int> track(bounds.getX() + 4,
                                   bounds.getCentreY() - trackHeight / 2,
                                   bounds.getWidth() - 8,
                                   trackHeight);

        int thumbWidth = juce::jmin(52, track.getWidth() / 2);
        int thumbHeight = 24;
        int thumbX = juce::jlimit(bounds.getX(), bounds.getRight() - thumbWidth,
                                  (int)sliderPos - thumbWidth / 2);
        juce::Rectangle<int> thumb(thumbX, track.getCentreY() - thumbHeight / 2, thumbWidth, thumbHeight);

        const juce::Colour base = editor != nullptr ? editor->faderThemeColour : juce::Colour(0xff666666);
        juce::ColourGradient grad(base.brighter(0.5f), (float)thumb.getX(), (float)thumb.getY(),
                                  base.darker(0.4f), (float)thumb.getRight(), (float)thumb.getBottom(), false);
        if (editor != nullptr && normaliseColourPresetName(editor->faderColourPreset).startsWith("multi"))
            grad.addColour(0.4, juce::Colour::fromHSV((float)slider.getValue() * 0.8f, 0.8f, 1.0f, 1.0f));
        else
            grad.addColour(0.4, base.brighter(0.2f));
        g.setGradientFill(grad);
        g.fillRect(thumb);

        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawRect(thumb);

        g.setColour(juce::Colours::white.withAlpha(0.4f));
        int innerX = thumb.getX() + 4;
        for (int i = 0; i < 4; ++i)
        {
            g.drawLine((float)innerX, (float)(thumb.getY() + 2),
                       (float)innerX, (float)(thumb.getBottom() - 2));
            innerX += 4;
        }

        double v = slider.getValue();
        double db = (v <= 1.0e-6) ? -60.0 : 20.0 * std::log10(v);
        int dbInt = (int)std::round(db);

        juce::Rectangle<int> box(thumb.getX(), thumb.getY() - 16, thumb.getWidth(), 14);
        g.setColour(juce::Colours::black);
        g.fillRect(box);
        g.setColour(juce::Colours::white.withAlpha(0.9f));

        juce::String text;
        if (v <= 1.0e-6)
            text = "-inf";
        else if (dbInt > 0)
            text = "+" + juce::String(dbInt) + " dB";
        else
            text = juce::String(dbInt) + " dB";

        g.setFont(10.0f);
        g.drawText(text, box, juce::Justification::centred, false);
    }
    else
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
}

class ServerListComponent : public juce::Component,
                            public juce::ListBoxModel
{
public:
    ServerListComponent(NinjamVst3AudioProcessor& p,
                        std::function<void(const juce::String&)> onChooseServer,
                        std::function<void(const juce::String&)> onConnectServer)
        : processor(p),
          onServerChosen(std::move(onChooseServer)),
          onServerConnect(std::move(onConnectServer))
    {
        addAndMakeVisible(listBox);
        listBox.setModel(this);
        listBox.setRowHeight(24);

        addAndMakeVisible(refreshButton);
        refreshButton.setButtonText("Refresh");
        refreshButton.onClick = [this] { refreshServers(); };

        addAndMakeVisible(connectButton);
        connectButton.setButtonText("Set Server");
        connectButton.onClick = [this]
        {
            int row = listBox.getSelectedRow();
            chooseServer(row);
        };

        refreshServers();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto controls = area.removeFromBottom(30);
        refreshButton.setBounds(controls.removeFromLeft(100));
        controls.removeFromLeft(8);
        connectButton.setBounds(controls.removeFromLeft(120));
        listBox.setBounds(area);
    }

    int getNumRows() override { return (int)servers.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= (int)servers.size())
            return;

        auto& s = servers[(size_t)rowNumber];

        if (rowIsSelected)
            g.fillAll(juce::Colours::darkblue.withAlpha(0.6f));
        else
            g.fillAll(juce::Colours::darkgrey);

        g.setColour(juce::Colours::white);

        juce::String text;
        text << s.name << "  "
             << s.userCount << "/" << s.userMax
             << "  " << juce::String(s.bpm, 1) << " BPM"
             << " / " << s.bpi << " BPI";

        g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { connectServer(row); }

private:
    NinjamVst3AudioProcessor& processor;
    juce::ListBox listBox;
    juce::TextButton refreshButton;
    juce::TextButton connectButton;
    std::vector<NinjamVst3AudioProcessor::PublicServerInfo> servers;
    std::function<void(const juce::String&)> onServerChosen;
    std::function<void(const juce::String&)> onServerConnect;

    void refreshServers()
    {
        processor.refreshPublicServers();
        servers = processor.getPublicServers();
        listBox.updateContent();
        repaint();
    }

    void chooseServer(int row)
    {
        if (row < 0 || row >= (int)servers.size())
            return;
        auto& s = servers[(size_t)row];
        juce::String hostPort = s.host + ":" + juce::String(s.port);
        if (onServerChosen)
            onServerChosen(hostPort);
    }

    void connectServer(int row)
    {
        if (row < 0 || row >= (int)servers.size())
            return;
        auto& s = servers[(size_t)row];
        juce::String hostPort = s.host + ":" + juce::String(s.port);
        if (onServerConnect)
            onServerConnect(hostPort);
    }
};

class ServerListWindow : public juce::DocumentWindow
{
public:
    ServerListWindow(NinjamVst3AudioProcessor& p,
                     std::function<void(const juce::String&)> onChooseServer,
                     std::function<void(const juce::String&)> onConnectServer)
        : DocumentWindow("NINJAM Servers", juce::Colours::black, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(new ServerListComponent(p,
                                                std::move(onChooseServer),
                                                std::move(onConnectServer)), true);
        centreWithSize(600, 400);
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }
};

// Forward declarations (defined after ChatWindow)
static juce::Colour senderColour(const juce::String& sender);
static void applyColoredChat(juce::TextEditor&, const juce::StringArray&, const juce::StringArray&);

class ChatPopupComponent : public juce::Component
{
public:
    ChatPopupComponent(NinjamVst3AudioProcessor& p) : processor(p)
    {
        addAndMakeVisible(chatDisplay);
        chatDisplay.setMultiLine(true);
        chatDisplay.setReadOnly(true);
        chatDisplay.setFont(juce::Font(14.0f));

        addAndMakeVisible(chatInput);
        chatInput.onReturnKey = [this] { sendClicked(); };

        addAndMakeVisible(sendButton);
        sendButton.setButtonText("Send");
        sendButton.onClick = [this] { sendClicked(); };

        addAndMakeVisible(atButton);
        atButton.setClickingTogglesState(true);
        atButton.setWantsKeyboardFocus(false);
        atButton.setToggleState(false, juce::dontSendNotification);
        atButton.setLookAndFeel(&atPopupBtnLAF);
        atButton.onClick = [this] { atToggled(); };
    }

    ~ChatPopupComponent() override
    {
        atButton.setLookAndFeel(nullptr);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto inputArea = area.removeFromBottom(30);
        auto atArea = inputArea.removeFromRight(40);
        auto sendArea = inputArea.removeFromRight(60);
        chatInput.setBounds(inputArea);
        sendButton.setBounds(sendArea);
        atButton.setBounds(atArea);
        chatDisplay.setBounds(area);
    }

    void setChatText(const juce::StringArray& lines, const juce::StringArray& senders)
    {
        applyColoredChat(chatDisplay, lines, senders);
    }

private:
    NinjamVst3AudioProcessor& processor;
    juce::TextEditor chatDisplay;
    juce::TextEditor chatInput;
    juce::TextButton sendButton;
    juce::TextButton atButton;
    ATButtonLookAndFeel atPopupBtnLAF;

    void sendClicked()
    {
        auto msg = chatInput.getText();
        if (msg.isNotEmpty())
        {
            processor.sendChatMessage(msg);
            chatInput.clear();
        }
    }

    void atToggled() { processor.setAutoTranslateEnabled(atButton.getToggleState()); }
};

class ChatWindow : public juce::DocumentWindow
{
public:
    ChatWindow(NinjamVst3AudioProcessor& p, std::function<void()> onClosedCallback)
        : DocumentWindow("NINJAM Chat", juce::Colours::black, DocumentWindow::closeButton),
          onClosed(std::move(onClosedCallback))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(new ChatPopupComponent(p), true);
        centreWithSize(500, 400);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        if (onClosed)
            onClosed();
    }

private:
    std::function<void()> onClosed;
};

// --- Chat colour helpers ---
static juce::Colour senderColour(const juce::String& sender)
{
    if (sender == "me")
        return juce::Colours::white;
    if (sender.isEmpty())
        return juce::Colour::fromRGB(160, 160, 120);   // dim amber – system

    // Deterministic hash → one of 8 distinct palette colours
    uint32_t h = 5381u;
    for (auto c : sender)
        h = h * 33u ^ (uint32_t)juce::CharacterFunctions::toUpperCase(c);

    static const juce::Colour palette[] = {
        juce::Colour::fromRGB(100, 180, 255),  // blue
        juce::Colour::fromRGB( 80, 210, 140),  // green
        juce::Colour::fromRGB(255, 165,  80),  // orange
        juce::Colour::fromRGB(190, 120, 255),  // purple
        juce::Colour::fromRGB( 80, 220, 215),  // teal
        juce::Colour::fromRGB(255, 130, 160),  // pink
        juce::Colour::fromRGB(230, 200,  80),  // gold
        juce::Colour::fromRGB(160, 200, 100),  // lime
    };
    return palette[h % 8u];
}

static juce::String normaliseColourPresetName(const juce::String& name)
{
    auto s = name.trim().toLowerCase();
    s = s.removeCharacters(" _-");
    return s;
}

static juce::Colour colourFromPresetName(const juce::String& preset, const juce::Colour& fallback)
{
    const auto key = normaliseColourPresetName(preset);
    if (key == "gold") return juce::Colour(0xffb8860b);
    if (key == "grey" || key == "gray") return juce::Colour(0xff909090);
    if (key == "sand" || key == "sandcolour" || key == "sandcolor") return juce::Colour(0xffc2b280);
    if (key == "yellow") return juce::Colour(0xffffd700);
    if (key == "orange") return juce::Colour(0xffff8c00);
    if (key == "red") return juce::Colour(0xffdc143c);
    if (key == "blue") return juce::Colour(0xff1e90ff);
    if (key == "pink") return juce::Colour(0xffff69b4);
    if (key == "purpleblue") return juce::Colour(0xff6a5acd);
    if (key == "black") return juce::Colour(0xff202020);
    if (key == "cream") return juce::Colour(0xfffff5dc);
    if (key == "white") return juce::Colour(0xfff2f2f2);
    return fallback;
}

static void applyColoredChat(juce::TextEditor& display,
                             const juce::StringArray& lines,
                             const juce::StringArray& senders)
{
    display.setReadOnly(false);
    display.clear();
    const int n = lines.size();
    for (int i = 0; i < n; ++i)
    {
        const juce::String& sndr = (i < senders.size()) ? senders[i] : juce::String();
        display.setColour(juce::TextEditor::textColourId, senderColour(sndr));
        display.insertTextAtCaret(lines[i] + "\n");
    }
    display.setReadOnly(true);
    display.moveCaretToEnd();
}
// --- end chat helpers ---

void CustomKnobLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, const float rotaryStartAngle,
                                              const float rotaryEndAngle, juce::Slider& slider)
{
    auto centreX = (float)x + (float)width * 0.5f;
    auto centreY = (float)y + (float)height * 0.5f;

    auto* editor = dynamic_cast<NinjamVst3AudioProcessorEditor*>(slider.getParentComponent());
    if (editor == nullptr)
    {
        auto* p = slider.getParentComponent();
        while (p != nullptr && editor == nullptr)
        {
            editor = dynamic_cast<NinjamVst3AudioProcessorEditor*>(p);
            p = p->getParentComponent();
        }
    }

    if (editor != nullptr && editor->radioKnobImage.isValid())
    {
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float radius = (float)juce::jmin(width / 2, height / 2) - 1.0f;
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(angle, centreX, centreY));
        g.drawImageWithin(editor->radioKnobImage,
                          (int)(centreX - radius), (int)(centreY - radius),
                          (int)(radius * 2.0f), (int)(radius * 2.0f),
                          juce::RectanglePlacement::fillDestination);
        g.restoreState();
        return;
    }

    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float outerRadius = (float)juce::jmin(width / 2, height / 2) - 4.0f;

    const auto knobPreset = editor != nullptr ? normaliseColourPresetName(editor->knobColourPreset) : juce::String();
    const bool multiColourKnob = knobPreset.startsWith("multi");
    const juce::Colour knobBase = editor != nullptr ? editor->knobThemeColour : juce::Colours::grey;
    const juce::Colour ringFill = knobBase.darker(1.1f);
    const juce::Colour ringStroke = knobBase.darker(1.4f);
    const juce::Colour tickColour = multiColourKnob
        ? juce::Colour::fromHSV(sliderPos * 0.8f, 0.8f, 1.0f, 1.0f)
        : knobBase.brighter(0.1f);

    // --- Tick marks ---
    const int numTicks = 11;
    for (int i = 0; i < numTicks; ++i)
    {
        float tickAngle = rotaryStartAngle + (float)i / (float)(numTicks - 1) * (rotaryEndAngle - rotaryStartAngle);
        float s = std::sin(tickAngle), c = -std::cos(tickAngle);
        float inner = outerRadius + 3.0f;
        float outer = outerRadius + 7.0f;
        g.setColour(tickColour);
        g.drawLine(centreX + s * inner, centreY + c * inner,
                   centreX + s * outer, centreY + c * outer, 1.2f);
    }

    // --- Knurled outer ring ---
    const float ringRadius = outerRadius;
    const int teeth = 24;
    juce::Path ring;
    for (int i = 0; i <= teeth * 2; ++i)
    {
        float a = (float)i / (float)(teeth * 2) * juce::MathConstants<float>::twoPi;
        float r = (i % 2 == 0) ? ringRadius : ringRadius - 3.0f;
        float px = centreX + std::sin(a) * r;
        float py = centreY - std::cos(a) * r;
        if (i == 0) ring.startNewSubPath(px, py);
        else        ring.lineTo(px, py);
    }
    ring.closeSubPath();
    g.setColour(ringFill);
    g.fillPath(ring);
    g.setColour(ringStroke);
    g.strokePath(ring, juce::PathStrokeType(0.8f));

    // --- Inner cap with radial gradient ---
    const float capRadius = ringRadius - 5.0f;
    const juce::Colour capHighlight = multiColourKnob
        ? juce::Colour::fromHSV(sliderPos * 0.8f, 0.7f, 1.0f, 1.0f)
        : knobBase.brighter(0.75f);
    const juce::Colour capShadow = multiColourKnob
        ? juce::Colour::fromHSV(sliderPos * 0.8f, 0.9f, 0.45f, 1.0f)
        : knobBase.darker(0.8f);
    juce::ColourGradient capGrad(capHighlight, centreX - capRadius * 0.35f, centreY - capRadius * 0.35f,
                                 capShadow, centreX + capRadius * 0.5f,  centreY + capRadius * 0.6f, true);
    capGrad.addColour(0.45, knobBase.brighter(0.35f));
    g.setGradientFill(capGrad);
    g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    // Subtle rim shadow on cap
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.5f);

    // Specular highlight (top-left arc)
    juce::Path highlight;
    highlight.addArc(centreX - capRadius * 0.65f, centreY - capRadius * 0.65f,
                     capRadius * 1.3f, capRadius * 1.3f,
                     -juce::MathConstants<float>::pi * 0.9f,
                     -juce::MathConstants<float>::pi * 0.2f, true);
    g.setColour(juce::Colours::white.withAlpha(multiColourKnob ? 0.35f : 0.28f));
    g.strokePath(highlight, juce::PathStrokeType(capRadius * 0.18f));

    // --- Indicator line ---
    const float lineStart = capRadius * 0.22f;
    const float lineEnd   = capRadius * 0.82f;
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.drawLine(centreX + std::sin(angle) * lineStart, centreY - std::cos(angle) * lineStart,
               centreX + std::sin(angle) * lineEnd,   centreY - std::cos(angle) * lineEnd,
               2.2f);
    // Bright dot at tip
    g.setColour(juce::Colours::white);
    float dotX = centreX + std::sin(angle) * lineEnd;
    float dotY = centreY - std::cos(angle) * lineEnd;
    g.fillEllipse(dotX - 2.0f, dotY - 2.0f, 4.0f, 4.0f);
}

NinjamVst3AudioProcessorEditor::NinjamVst3AudioProcessorEditor (NinjamVst3AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), intervalDisplay(p), userList(p)
{
    setSize (isAbletonLiveHost() ? 1240 : 1350, 600);
    setResizable(true, true);
    setResizeLimits(900, 500, 2200, 1500);

    juce::LookAndFeel::setDefaultLookAndFeel(&outlinedLabelLAF);

    serverLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(serverLabel);
    serverField.setText("");
    serverField.setIndents(4, 8);
    serverField.onReturnKey = [this] { connectClicked(); };
    addAndMakeVisible(serverField);

    addAndMakeVisible(serverListButton);
    serverListButton.setButtonText("Servers");
    serverListButton.setTooltip("Click to View Servers");
    serverListButton.onClick = [this] { serverListClicked(); };

    userLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(userLabel);
    userField.setText("user" + juce::String(juce::Random::getSystemRandom().nextInt(100)));
    userField.setIndents(4, 8);
    addAndMakeVisible(userField);

    addAndMakeVisible(anonymousButton);
    anonymousButton.setToggleState(true, juce::dontSendNotification);
    anonymousButton.onClick = [this] { anonymousToggled(); };

    passLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(passLabel);
    addAndMakeVisible(passField);
    passField.setIndents(4, 8);
    passField.setEnabled(false);

    addAndMakeVisible(connectButton);
    connectButton.setButtonText("Connect");
    connectButton.onClick = [this] { connectClicked(); };

    addAndMakeVisible(statusLabel);

    addAndMakeVisible(transmitButton);
    transmitButton.setClickingTogglesState(true);
    transmitButton.onClick = [this] { transmitToggled(); };
    updateTransmitButtonColor();

    addAndMakeVisible(localMonitorButton);
    localMonitorButton.setClickingTogglesState(true);
    localMonitorButton.onClick = [this]
    {
        audioProcessor.setLocalMonitorEnabled(localMonitorButton.getToggleState());
        updateMonitorButtonColor();
    };
    updateMonitorButtonColor();

    addAndMakeVisible(voiceChatButton);
    voiceChatButton.setClickingTogglesState(true);
    voiceChatButton.setToggleState(false, juce::dontSendNotification);
    voiceChatButton.onClick = [this]
    {
        audioProcessor.setVoiceChatMode(voiceChatButton.getToggleState());
        voiceChatGlowPhase = 0.0f;
        updateVoiceChatButtonColor();
    };
    updateVoiceChatButtonColor();

    bitrateSelector.addItem("64 kbps",  1);
    bitrateSelector.addItem("96 kbps",  2);
    bitrateSelector.addItem("128 kbps", 3);
    bitrateSelector.addItem("160 kbps", 4);
    bitrateSelector.addItem("192 kbps", 5);
    bitrateSelector.addItem("256 kbps", 6);
    bitrateSelector.addItem("320 kbps", 7);
    bitrateSelector.setSelectedId(3, juce::dontSendNotification); // 128 kbps default
    bitrateSelector.onChange = [this]
    {
        const int bitrateValues[] = { 64, 96, 128, 160, 192, 256, 320 };
        int idx = bitrateSelector.getSelectedId() - 1;
        if (idx >= 0 && idx < 7)
            audioProcessor.setLocalBitrate(bitrateValues[idx]);
    };
    addAndMakeVisible(bitrateSelector);
    bitrateSelector.setTooltip("Quality");

    addAndMakeVisible(midiRelayTargetSelector);
    midiRelayTargetSelector.setTooltip("Send Midi");
    midiRelayTargetSelector.onChange = [this]
    {
        int id = midiRelayTargetSelector.getSelectedId();
        auto it = midiRelayTargetByMenuId.find(id);
        if (it != midiRelayTargetByMenuId.end())
            audioProcessor.setMidiRelayTarget(it->second);
    };
    refreshMidiRelayTargetSelector();
    addListener(this);
    if (!connect(9001))
        for (int port = 9002; port <= 9010; ++port)
            if (connect(port))
                break;

    addAndMakeVisible(videoButton);
    videoButton.setTooltip("VDO Synced Video");
    videoButton.onClick = [this] { videoClicked(); };

    addAndMakeVisible(layoutButton);
    layoutButton.setClickingTogglesState(true);
    layoutButton.setTooltip("Vertical Mixer");
    layoutButton.setLookAndFeel(&faderIconLookAndFeel);
    layoutButton.onClick = [this] { layoutToggled(); updateLayoutButtonColor(); };
    updateLayoutButtonColor();

    addAndMakeVisible(autoLevelButton);
    autoLevelButton.setClickingTogglesState(true);
    autoLevelButton.setTooltip("Auto Adjust Volume");
    autoLevelButton.onClick = [this]
    {
        bool newState = autoLevelButton.getToggleState();
        if (newState == autoLevelEnabled)
            return;

        autoLevelEnabled = newState;
        if (!autoLevelEnabled)
        {
            auto users = audioProcessor.getConnectedUsers();
            for (auto& u : users)
                audioProcessor.setUserVolume(u.index, u.volume);

            autoLevelCurrentGains.clear();
            autoLevelPeakLevels.clear();
            autoLevelChannelActiveTicks.clear();
        }
        updateAutoLevelButtonColor();
    };
    updateAutoLevelButtonColor();

    addAndMakeVisible(metronomeLabel);
    addAndMakeVisible(metronomeSlider);
    metronomeSlider.setRange(0.0, 1.0);
    metronomeSlider.setValue(0.5);
    metronomeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    metronomeSlider.setTooltip("Metronome Volume");
    metronomeSlider.onValueChange = [this] { metronomeChanged(); };

    addAndMakeVisible(metronomeMuteButton);
    metronomeMuteButton.setClickingTogglesState(true);
    metronomeMuteButton.setToggleState(true, juce::dontSendNotification);  // starts unmuted
    metronomeMuteButton.setTooltip("Mute Metronome");
    metronomeMuteButton.setLookAndFeel(&metronomeBtnLAF);
    metronomeMuteButton.onClick = [this]
    {
        if (metronomeMuteButton.getToggleState())
        {
            // unmuting: restore stored volume
            metronomeSlider.setValue(storedMetronomeVolume, juce::dontSendNotification);
            audioProcessor.setMetronomeVolume(storedMetronomeVolume);
        }
        else
        {
            // muting: store current volume and silence
            storedMetronomeVolume = (float)metronomeSlider.getValue();
            audioProcessor.setMetronomeVolume(0.0f);
        }
        updateMetronomeButtonColor();
    };
    updateMetronomeButtonColor();

    addAndMakeVisible(syncButton);
    syncButton.setClickingTogglesState(true);
    syncButton.setToggleState(false, juce::dontSendNotification);
#if JucePlugin_Build_Standalone
    syncButton.setTooltip("Click to Sync to Midi Clock");
#else
    syncButton.setTooltip("Click to Sync to Host (vst)");
#endif
    syncButton.setLookAndFeel(&syncIconLAF);
    syncButton.onClick = [this] { syncToggled(); updateSyncButtonColor(); };
    updateSyncButtonColor();

    addAndMakeVisible(fxButton);
    fxButton.onClick = [this] { showFxMenu(); };
    updateFxButtonLabel();
    addAndMakeVisible(optionsButton);
    optionsButton.onClick = [this] { showOptionsMenu(); };

    addAndMakeVisible(tempoLabel);
    tempoLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(chatButton);
    chatButton.setClickingTogglesState(true);
    chatButton.setWantsKeyboardFocus(false);
    chatButton.setToggleState(false, juce::dontSendNotification);
    chatButton.setTooltip("Open Chat");
    chatButton.setLookAndFeel(&chatBtnLAF);
    chatButton.onClick = [this] { chatToggled(); };
    updateChatButtonColor();

    addAndMakeVisible(usersLabel);
    addAndMakeVisible(spreadOutputsButton);
    spreadOutputsButton.setClickingTogglesState(true);
    spreadOutputsButton.setToggleState(false, juce::dontSendNotification);
    spreadOutputsButton.onClick = [this]
    {
        audioProcessor.setSpreadOutputsEnabled(spreadOutputsButton.getToggleState());
    };
    addAndMakeVisible(userList);

    addAndMakeVisible(addLocalChannelButton);
    addLocalChannelButton.setTooltip("Add Channel");
    addAndMakeVisible(removeLocalChannelButton);
    removeLocalChannelButton.setTooltip("Remove Channel");
    addAndMakeVisible(localFaderLabel);
    localFaderLabel.setJustificationType(juce::Justification::centred);

    addLocalChannelButton.onClick = [this]
    {
        int current = audioProcessor.getNumLocalChannels();
        if (current < NinjamVst3AudioProcessor::maxLocalChannels)
        {
            audioProcessor.setNumLocalChannels(current + 1);
            for (int i = 0; i < NinjamVst3AudioProcessor::maxLocalChannels; ++i)
                localChannelNameLabels[(size_t)i].setText(audioProcessor.getLocalChannelName(i), juce::dontSendNotification);
            resized();
        }
    };

    removeLocalChannelButton.onClick = [this]
    {
        int current = audioProcessor.getNumLocalChannels();
        if (current > 1)
        {
            audioProcessor.setNumLocalChannels(current - 1);
            for (int i = 0; i < NinjamVst3AudioProcessor::maxLocalChannels; ++i)
                localChannelNameLabels[(size_t)i].setText(audioProcessor.getLocalChannelName(i), juce::dontSendNotification);
            resized();
        }
    };

    int totalInputs = audioProcessor.getTotalNumInputChannels();
    if (totalInputs <= 0)
        totalInputs = 2;
    int numPairs = totalInputs / 2;

    for (int i = 0; i < NinjamVst3AudioProcessor::maxLocalChannels; ++i)
    {
        addAndMakeVisible(localFaders[(size_t)i]);
        addAndMakeVisible(localPeakMeters[(size_t)i]);
        addAndMakeVisible(localInputSelectors[(size_t)i]);
        addAndMakeVisible(localInputModeSelectors[(size_t)i]);
        addAndMakeVisible(localDbLabels[(size_t)i]);
        addAndMakeVisible(localReverbSendKnobs[(size_t)i]);
        addAndMakeVisible(localDelaySendKnobs[(size_t)i]);
        addAndMakeVisible(localReverbSendLabels[(size_t)i]);
        addAndMakeVisible(localDelaySendLabels[(size_t)i]);

        // Editable channel name label
        auto& nameLabel = localChannelNameLabels[(size_t)i];
        nameLabel.setText(audioProcessor.getLocalChannelName(i), juce::dontSendNotification);
        nameLabel.setEditable(true, false); // single-click to edit
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        nameLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1a1a));
        nameLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xff333333));
        nameLabel.setTooltip("Click to name this channel");
        int ch = i;
        nameLabel.onTextChange = [this, ch]
        {
            audioProcessor.setLocalChannelName(ch, localChannelNameLabels[(size_t)ch].getText());
        };
        addAndMakeVisible(nameLabel);

        auto& fader = localFaders[(size_t)i];
        fader.setSliderStyle(juce::Slider::LinearVertical);
        fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange(0.0, 2.0);
        fader.setSkewFactorFromMidPoint(0.25);
        fader.setValue(audioProcessor.getLocalChannelGain(i), juce::dontSendNotification);
        fader.setDoubleClickReturnValue(true, 1.0);
        fader.setLookAndFeel(&mixerFaderLookAndFeel);

        fader.onValueChange = [this, i]
        {
            float value = (float)localFaders[(size_t)i].getValue();
            audioProcessor.setLocalChannelGain(i, value);
            if (i == 0)
                audioProcessor.setLocalInputGain(value);
        };
        registerMidiLearnTarget(fader, "local.fader." + juce::String(i + 1), false);

        auto& revSend = localReverbSendKnobs[(size_t)i];
        revSend.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        revSend.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        revSend.setRange(0.0, 1.0);
        revSend.setValue(audioProcessor.getLocalChannelReverbSend(i), juce::dontSendNotification);
        revSend.setLookAndFeel(&customKnobLookAndFeel);
        revSend.setTooltip("Reverb Send");
        revSend.onValueChange = [this, i]
        {
            audioProcessor.setLocalChannelReverbSend(i, (float)localReverbSendKnobs[(size_t)i].getValue());
        };
        registerMidiLearnTarget(revSend, "local.send.reverb." + juce::String(i + 1), false);

        auto& dlySend = localDelaySendKnobs[(size_t)i];
        dlySend.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        dlySend.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        dlySend.setRange(0.0, 1.0);
        dlySend.setValue(audioProcessor.getLocalChannelDelaySend(i), juce::dontSendNotification);
        dlySend.setLookAndFeel(&customKnobLookAndFeel);
        dlySend.setTooltip("Delay Send");
        dlySend.onValueChange = [this, i]
        {
            audioProcessor.setLocalChannelDelaySend(i, (float)localDelaySendKnobs[(size_t)i].getValue());
        };
        registerMidiLearnTarget(dlySend, "local.send.delay." + juce::String(i + 1), false);

        auto& revLbl = localReverbSendLabels[(size_t)i];
        revLbl.setText("Rev", juce::dontSendNotification);
        revLbl.setJustificationType(juce::Justification::centred);
        revLbl.setFont(juce::Font(9.0f));

        auto& dlyLbl = localDelaySendLabels[(size_t)i];
        dlyLbl.setText("Dly", juce::dontSendNotification);
        dlyLbl.setJustificationType(juce::Justification::centred);
        dlyLbl.setFont(juce::Font(9.0f));

        auto& selector = localInputSelectors[(size_t)i];
        selector.clear(juce::dontSendNotification);

        for (int ch = 0; ch < totalInputs; ++ch)
            selector.addItem("In " + juce::String(ch + 1), ch + 1);

        int stereoBaseId = 100;
        for (int pair = 0; pair < numPairs; ++pair)
        {
            int left = pair * 2 + 1;
            int right = left + 1;
            selector.addItem(juce::String(left) + "/" + juce::String(right), stereoBaseId + pair);
        }

        int currentInput = audioProcessor.getLocalChannelInput(i);
        if (currentInput >= 0 && currentInput < totalInputs)
        {
            selector.setSelectedId(currentInput + 1, juce::dontSendNotification);
        }
        else if (currentInput < 0)
        {
            int pairIndex = -1 - currentInput;
            if (numPairs > pairIndex)
            {
                selector.setSelectedId(stereoBaseId + pairIndex, juce::dontSendNotification);
            }
            else if (numPairs > 0)
            {
                // Preferred pair unavailable, use first available stereo pair
                selector.setSelectedId(stereoBaseId, juce::dontSendNotification);
                audioProcessor.setLocalChannelInput(i, -1);
            }
            else if (totalInputs > 0)
            {
                // No stereo pairs at all, fall back to mono channel 1
                selector.setSelectedId(1, juce::dontSendNotification);
                audioProcessor.setLocalChannelInput(i, 0);
            }
        }

        selector.onChange = [this, i]
        {
            int id = localInputSelectors[(size_t)i].getSelectedId();
            if (id <= 0)
                return;

            int total = audioProcessor.getTotalNumInputChannels();
            if (total <= 0)
                total = 2;
            int numPairsLocal = total / 2;
            int stereoBase = 100;

            if (id >= 1 && id <= total)
            {
                audioProcessor.setLocalChannelInput(i, id - 1);
                applyRemoteMidiRelaySelection(i, id - 1);
                localInputModeSelectors[(size_t)i].setSelectedId(1, juce::dontSendNotification);
            }
            else if (id >= stereoBase && id < stereoBase + numPairsLocal)
            {
                int pairIndex = id - stereoBase;
                audioProcessor.setLocalChannelInput(i, -1 - pairIndex);
                applyRemoteMidiRelaySelection(i, -1 - pairIndex);
                localInputModeSelectors[(size_t)i].setSelectedId(2, juce::dontSendNotification);
            }
        };

        auto& modeSelector = localInputModeSelectors[(size_t)i];
        modeSelector.addItem("Mono", 1);
        modeSelector.addItem("Stereo", 2);
        modeSelector.setSelectedId(currentInput < 0 ? 2 : 1, juce::dontSendNotification);
    }

    addAndMakeVisible(masterFaderLabel);
    masterFaderLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterFader);
    masterFader.setSliderStyle(juce::Slider::LinearVertical);
    masterFader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterFader.setRange(0.0, 2.0);
    masterFader.setSkewFactorFromMidPoint(0.25);
    masterFader.setValue(1.0, juce::dontSendNotification);
    masterFader.setDoubleClickReturnValue(true, 1.0);
    masterFader.setLookAndFeel(&mixerFaderLookAndFeel);
    masterFader.onValueChange = [this]
    {
        audioProcessor.setMasterOutputGain((float)masterFader.getValue());
    };
    registerMidiLearnTarget(masterFader, "master.fader", false);
    addAndMakeVisible(masterPeakMeter);
    addAndMakeVisible(masterDbLabel);
    masterDbLabel.setFont(juce::Font(9.0f));
    masterDbLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(limiterButton);
    addAndMakeVisible(limiterReleaseLabel);
    limiterReleaseLabel.setJustificationType(juce::Justification::centred);
    limiterReleaseLabel.setTooltip("Limiter Release Amount");
    limiterButton.setClickingTogglesState(true);
    limiterButton.setTooltip("Master Limiter Gain");
    limiterButton.setToggleState(false, juce::dontSendNotification);
    limiterButton.onClick = [this]
    {
        audioProcessor.setMasterLimiterEnabled(limiterButton.getToggleState());
        updateLimiterButtonColor();
    };
    updateLimiterButtonColor();
    addAndMakeVisible(limiterThresholdSlider);
    limiterThresholdSlider.setSliderStyle(juce::Slider::LinearVertical);
    limiterThresholdSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    limiterThresholdSlider.setRange(-6.0, 0.0);
    limiterThresholdSlider.setValue(0.0, juce::dontSendNotification);
    limiterThresholdSlider.setLookAndFeel(&mixerFaderLookAndFeel);
    limiterThresholdSlider.onValueChange = [this]
    {
        audioProcessor.setLimiterThreshold((float)limiterThresholdSlider.getValue());
    };
    registerMidiLearnTarget(limiterThresholdSlider, "limiter.threshold", false);

    addAndMakeVisible(limiterReleaseSlider);
    limiterReleaseSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    limiterReleaseSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    limiterReleaseSlider.setRange(10.0, 1000.0);
    limiterReleaseSlider.setValue(100.0, juce::dontSendNotification);
    limiterReleaseSlider.setTooltip("Limiter Release Amount");
    limiterReleaseSlider.setLookAndFeel(&customKnobLookAndFeel);
    limiterReleaseSlider.onValueChange = [this]
    {
        audioProcessor.setLimiterRelease((float)limiterReleaseSlider.getValue());
    };
    registerMidiLearnTarget(limiterReleaseSlider, "limiter.release", false);

    addAndMakeVisible(delayTimeLabel);
    delayTimeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(delayTimeSlider);
    delayTimeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    delayTimeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    delayTimeSlider.setRange(20.0, 2000.0);
    delayTimeSlider.setValue(audioProcessor.getFxDelayTimeMs(), juce::dontSendNotification);
    delayTimeSlider.setLookAndFeel(&customKnobLookAndFeel);
    delayTimeSlider.onValueChange = [this]
    {
        audioProcessor.setFxDelayTimeMs((float)delayTimeSlider.getValue());
    };
    registerMidiLearnTarget(delayTimeSlider, "fx.delay.time", false);

    addAndMakeVisible(delayDivisionSelector);
    delayDivisionSelector.addItem("1/16", 16);
    delayDivisionSelector.addItem("1/8", 8);
    delayDivisionSelector.addItem("1/1", 1);
    delayDivisionSelector.setSelectedId(audioProcessor.getFxDelayDivision(), juce::dontSendNotification);
    delayDivisionSelector.onChange = [this]
    {
        int division = delayDivisionSelector.getSelectedId();
        if (division <= 0)
            division = 8;
        audioProcessor.setFxDelayDivision(division);
    };

    addAndMakeVisible(delayPingPongButton);
    delayPingPongButton.setClickingTogglesState(true);
    delayPingPongButton.setToggleState(audioProcessor.isFxDelayPingPong(), juce::dontSendNotification);
    delayPingPongButton.onClick = [this]
    {
        audioProcessor.setFxDelayPingPong(delayPingPongButton.getToggleState());
    };
    registerMidiLearnTarget(delayPingPongButton, "fx.delay.pingpong", true);

    addAndMakeVisible(chatDisplay);
    chatDisplay.setMultiLine(true);
    chatDisplay.setReadOnly(true);
    chatDisplay.setFont(juce::Font(14.0f));

    addAndMakeVisible(chatInput);
    chatInput.onReturnKey = [this] { sendClicked(); };

    addAndMakeVisible(sendButton);
    sendButton.onClick = [this] { sendClicked(); };

    addAndMakeVisible(atButton);
    atButton.setClickingTogglesState(true);
    atButton.setWantsKeyboardFocus(false);
    atButton.setToggleState(false, juce::dontSendNotification);
    atButton.setLookAndFeel(&atBtnLAF);
    atButton.onClick = [this] { atToggled(); };

    addAndMakeVisible(chatPopoutButton);
    chatPopoutButton.setButtonText("Popout");
    chatPopoutButton.onClick = [this] { chatPopoutClicked(); };

    addAndMakeVisible(videoBgToggle);
    videoBgToggle.setToggleState(true, juce::dontSendNotification);
    videoBgToggle.onClick = [this]
    {
        int idx = backgroundSelector.getSelectedItemIndex();
        if (idx >= 0 && idx < textureFiles.size())
            loadControlImages(textureFiles[idx]);
    };

    addAndMakeVisible(backgroundSelector);
    backgroundSelector.setTooltip("Skin");
    {
        auto texturesDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                               .getParentDirectory().getChildFile("textures");
        if (texturesDir.isDirectory())
        {
            // Each subdirectory is a theme; its name shows in the dropdown
            auto dirs = texturesDir.findChildFiles(juce::File::findDirectories, false);
            dirs.sort();
            for (int i = 0; i < dirs.size(); ++i)
            {
                if (dirs[i].getFileName().equalsIgnoreCase("Skin Template"))
                    continue;
                // Only include dirs that contain a bg.* file
                auto bgFiles = dirs[i].findChildFiles(juce::File::findFiles, false, "bg.*");
                if (bgFiles.isEmpty()) continue;
                textureFiles.add(dirs[i]);
                backgroundSelector.addItem(dirs[i].getFileName(), textureFiles.size());
            }
        }
        if (backgroundSelector.getNumItems() == 0)
            backgroundSelector.addItem("Default", 1);

        // Determine which texture to select: saved preference > "Brushed Metal 1" > first item
        juce::PropertiesFile::Options popts;
        popts.applicationName     = "NINJAM VST3";
        popts.filenameSuffix      = "settings";
        popts.folderName          = "NINJAM VST3";
        popts.osxLibrarySubFolder = "Application Support";
        juce::PropertiesFile props(popts);
        juce::String savedTexture = props.getValue("texture", "");
        abletonWindowSizePreset = juce::jlimit(0, 2, props.getIntValue("abletonWindowSizePreset", 1));

        int selectIdx = -1;
        if (savedTexture.isNotEmpty())
            for (int i = 0; i < textureFiles.size(); ++i)
                if (textureFiles[i].getFileName() == savedTexture) { selectIdx = i; break; }
        if (selectIdx < 0)
            for (int i = 0; i < textureFiles.size(); ++i)
                if (textureFiles[i].getFileName() == "Brushed Metal 1") { selectIdx = i; break; }
        if (selectIdx < 0 && backgroundSelector.getNumItems() > 0)
            selectIdx = 0;

        if (selectIdx >= 0)
            backgroundSelector.setSelectedId(backgroundSelector.getItemId(selectIdx), juce::dontSendNotification);

        // Load the selected texture immediately
        if (selectIdx >= 0 && selectIdx < textureFiles.size())
            loadControlImages(textureFiles[selectIdx]);
    }
    backgroundSelector.onChange = [this]
    {
        int idx = backgroundSelector.getSelectedItemIndex();
        if (idx >= 0 && idx < textureFiles.size())
        {
            // Persist the user's choice
            juce::PropertiesFile::Options popts;
            popts.applicationName     = "NINJAM VST3";
            popts.filenameSuffix      = "settings";
            popts.folderName          = "NINJAM VST3";
            popts.osxLibrarySubFolder = "Application Support";
            juce::PropertiesFile props(popts);
            props.setValue("texture", textureFiles[idx].getFileName());
            props.saveIfNeeded();

            loadControlImages(textureFiles[idx]);
        }
        else
        {
            backgroundImage  = juce::Image();
            radioKnobImage   = juce::Image();
            faderKnobImage   = juce::Image();
            metronomeThemeColour = juce::Colour::fromRGB(80, 185, 255);
            windowThemeColour    = juce::Colour(0x00000000);
            applyThemeColours();
        }
        repaint();
    };

    if (isAbletonLiveHost() && !audioProcessor.isStandaloneWrapper())
        setAbletonWindowSizePreset(abletonWindowSizePreset);

    addAndMakeVisible(intervalDisplay);
    registerMidiLearnTarget(metronomeSlider, "metronome.level", false);
    registerMidiLearnTarget(transmitButton, "button.transmit", true);
    registerMidiLearnTarget(localMonitorButton, "button.monitor", true);
    registerMidiLearnTarget(voiceChatButton, "button.voicechat", true);
    registerMidiLearnTarget(metronomeMuteButton, "button.metronomemute", true);
    registerMidiLearnTarget(syncButton, "button.sync", true);
    registerMidiLearnTarget(chatButton, "button.chat", true);
    registerMidiLearnTarget(spreadOutputsButton, "button.spreadoutputs", true);
    registerMidiLearnTarget(autoLevelButton, "button.autolevel", true);
    registerMidiLearnTarget(limiterButton, "button.limiter", true);
    syncUserStripMidiTargets();
    updateFxControlsVisibility();
    loadLearnMappingsFromProcessor();
    refreshExternalMidiInputDevices();

    startTimer(30);
}

NinjamVst3AudioProcessorEditor::~NinjamVst3AudioProcessorEditor()
{
#if JUCE_WINDOWS
    videoFrameReader.reset();
#endif
    for (auto& pair : midiTargetsByComponent)
        if (pair.first != nullptr)
            pair.first->removeMouseListener(this);
    midiTargetsByComponent.clear();
    midiTargetsById.clear();
    midiSourceByTargetId.clear();
    oscSourceByTargetId.clear();
    midiLearnArmedTargetId.clear();
    oscLearnArmedTargetId.clear();
    midiLearnInputDevice.reset();
    midiRelayInputDevice.reset();
    openedMidiLearnInputDeviceId.clear();
    openedMidiRelayInputDeviceId.clear();
    stopTimer();
    disconnect();
    atButton.setLookAndFeel(nullptr);
    chatButton.setLookAndFeel(nullptr);
    metronomeMuteButton.setLookAndFeel(nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void NinjamVst3AudioProcessorEditor::paint (juce::Graphics& g)
{
    if (backgroundImage.isValid())
    {
        g.drawImageWithin(backgroundImage, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::fillDestination);
    }
    else
    {
        // Window Colour sets the app background; falls back to dark grey if not set
        juce::Colour base = (windowThemeColour.getAlpha() > 0) ? windowThemeColour : juce::Colour(0xff222222);
        g.fillAll(base);
    }
}

void NinjamVst3AudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // Helper: draw a small tight radial glow around any toggle button
    auto drawGlow = [&](juce::Button& btn, juce::Colour onColour, juce::Colour offColour)
    {
        if (!btn.isVisible()) return;
        bool isOn = btn.getToggleState();
        auto bc   = btn.getBounds().toFloat();
        auto centre = bc.getCentre();
        // Glow starts a few px outside the button edge
        float gap = 5.0f;
        float r   = bc.getWidth() * 0.55f + gap;   // compact radius
        juce::Colour col = isOn ? onColour : offColour;
        juce::ColourGradient grad(col, centre.x, centre.y,
                                  juce::Colours::transparentBlack, centre.x + r, centre.y, true);
        g.setGradientFill(grad);
        g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
    };

    drawGlow(transmitButton,     juce::Colour(0x5532cc60), juce::Colour(0x22154420));  // green
    drawGlow(localMonitorButton, juce::Colour(0x55ff3232), juce::Colour(0x22441515));  // red
    drawGlow(autoLevelButton,    juce::Colour(0x55ffdd20), juce::Colour(0x22443a10));  // yellow
    drawGlow(limiterButton,      juce::Colour(0x55ff3232), juce::Colour(0x22441515));  // red
    drawGlow(layoutButton,       juce::Colour(0x5520c8e8), juce::Colour(0x220a3240));  // teal
    drawGlow(metronomeMuteButton,
             metronomeThemeColour.withAlpha(0.33f),
             metronomeThemeColour.withMultipliedBrightness(0.10f).withAlpha(0.13f)); // themed
    drawGlow(syncButton,          juce::Colour(0x55ff9820), juce::Colour(0x22301808)); // orange
    if (atButton.isVisible())
        drawGlow(atButton,        juce::Colour(0x5550c8ff), juce::Colour(0x220a2840)); // sky blue
    drawGlow(chatButton,          juce::Colour(0x5550c8ff), juce::Colour(0x220a2840)); // sky blue
}

void NinjamVst3AudioProcessorEditor::resized()
{
    if (!audioProcessor.isStandaloneWrapper() && !applyingDeferredResizeLayout)
    {
        pendingDeferredResizeLayout = true;
        lastResizeEventMs = juce::Time::getMillisecondCounterHiRes();
        return;
    }

    auto area = getLocalBounds().reduced(10);

    // Bottom: Interval Display
    auto bottomRow = area.removeFromBottom(40);
    intervalDisplay.setBounds(bottomRow);
    area.removeFromBottom(10);

    auto topRow = area.removeFromTop(30);
    // Right side of top row: texture / video-bg controls only
    backgroundSelector.setBounds(topRow.removeFromRight(150));
    topRow.removeFromRight(4);
    videoBgToggle.setBounds(topRow.removeFromRight(90));
    topRow.removeFromRight(10);
    // Left side: server fields
    serverLabel.setBounds(topRow.removeFromLeft(75));
    serverField.setBounds(topRow.removeFromLeft(160));
    topRow.removeFromLeft(6);
    serverListButton.setBounds(topRow.removeFromLeft(72));
    topRow.removeFromLeft(6);
    userLabel.setBounds(topRow.removeFromLeft(55));
    userField.setBounds(topRow.removeFromLeft(90));
    topRow.removeFromLeft(6);
    anonymousButton.setBounds(topRow.removeFromLeft(110));
    topRow.removeFromLeft(6);
    passLabel.setBounds(topRow.removeFromLeft(52));
    passField.setBounds(topRow.removeFromLeft(80));
    topRow.removeFromLeft(6);
    connectButton.setBounds(topRow.removeFromLeft(80));
    topRow.removeFromLeft(10);
    statusLabel.setBounds(topRow);

    area.removeFromTop(4);

    // Controls Row: layout, auto-level, metronome, tempo — chat+video buttons on the right
    auto controlsRow = area.removeFromTop(30);
    videoButton.setBounds(controlsRow.removeFromRight(100));
    controlsRow.removeFromRight(5);
    chatButton.setBounds(controlsRow.removeFromRight(80));
    controlsRow.removeFromRight(10);
    layoutButton.setBounds(controlsRow.removeFromLeft(40));  // icon-only button
    controlsRow.removeFromLeft(10);
    autoLevelButton.setBounds(controlsRow.removeFromLeft(110));
    controlsRow.removeFromLeft(10);
    metronomeLabel.setBounds(controlsRow.removeFromLeft(90));
    metronomeSlider.setBounds(controlsRow.removeFromLeft(80));
    auto metBtn = controlsRow.removeFromLeft(24);
    metronomeMuteButton.setBounds(metBtn.reduced(0, 3));
    controlsRow.removeFromLeft(6);
    auto syncBtn = controlsRow.removeFromLeft(24);
    syncButton.setBounds(syncBtn.reduced(0, 3));
    controlsRow.removeFromLeft(10);
    fxButton.setBounds(controlsRow.removeFromLeft(70));
    controlsRow.removeFromLeft(8);
    optionsButton.setBounds(controlsRow.removeFromLeft(78));
    controlsRow.removeFromLeft(8);
    tempoLabel.setBounds(controlsRow);

    area.removeFromTop(10);

    bool showDockedChat = chatButton.getToggleState() && !chatPoppedOut;
    juce::Rectangle<int> chatArea;
    if (showDockedChat)
    {
        auto chatWidth = (int)(area.getWidth() * 0.20f);
        chatArea = area.removeFromRight(chatWidth);
        area.removeFromRight(10);
    }

    int numLocal = audioProcessor.getNumLocalChannels();
    numLocal = juce::jlimit(1, NinjamVst3AudioProcessor::maxLocalChannels, numLocal);

    int baseLocalWidth = 110;
    int extraPerTrack = 40;
    int localWidth = baseLocalWidth + (numLocal - 1) * extraPerTrack;
    int maxLocalWidth = area.getWidth() / 2;
    if (localWidth > maxLocalWidth)
        localWidth = maxLocalWidth;

    int masterWidth = 190;

    auto localArea = area.removeFromLeft(localWidth);
    auto masterArea = area.removeFromRight(masterWidth);
    auto userArea = area;

    auto usersHeader = userArea.removeFromTop(20);
    spreadOutputsButton.setBounds(usersHeader.removeFromLeft(110));
    usersLabel.setBounds(usersHeader);
    userList.setBounds(userArea);

    // Transmit above local channels, monitor below it
    transmitButton.setBounds(localArea.removeFromTop(26));
    localArea.removeFromTop(3);
    localMonitorButton.setBounds(localArea.removeFromTop(26));
    localArea.removeFromTop(3);
    {
        auto row = localArea.removeFromTop(26);
        auto third = row.getWidth() / 3;
        voiceChatButton.setBounds(row.removeFromLeft(third));
        bitrateSelector.setBounds(row.removeFromLeft(third));
        midiRelayTargetSelector.setBounds(row);
    }
    localArea.removeFromTop(3);

    auto localHeader = localArea.removeFromTop(20);
    addLocalChannelButton.setBounds(localHeader.removeFromLeft(20));
    removeLocalChannelButton.setBounds(localHeader.removeFromLeft(20));
    localFaderLabel.setBounds(localHeader);
    auto localInner = localArea.reduced(4);

    int meterWidth = 10;
    int totalWidth = localInner.getWidth();
    int columnWidth = totalWidth / numLocal;

    for (int i = 0; i < NinjamVst3AudioProcessor::maxLocalChannels; ++i)
    {
        bool visible = i < numLocal;
        localFaders[(size_t)i].setVisible(visible);
        localPeakMeters[(size_t)i].setVisible(visible);
        localInputSelectors[(size_t)i].setVisible(visible);
        localInputModeSelectors[(size_t)i].setVisible(visible);
        localDbLabels[(size_t)i].setVisible(visible);
        localChannelNameLabels[(size_t)i].setVisible(visible);
        localReverbSendKnobs[(size_t)i].setVisible(visible);
        localDelaySendKnobs[(size_t)i].setVisible(visible);
        localReverbSendLabels[(size_t)i].setVisible(visible);
        localDelaySendLabels[(size_t)i].setVisible(visible);
    }

    for (int i = 0; i < numLocal; ++i)
    {
        juce::Rectangle<int> col = localInner.removeFromLeft(columnWidth);
        auto meterArea = col.removeFromLeft(meterWidth);
        auto nameArea = col.removeFromTop(18);
        auto dbArea = col.removeFromBottom(16);
        auto inputArea = col.removeFromBottom(20);
        auto inputModeArea = col.removeFromBottom(20);
        auto sendArea = col.removeFromBottom(36);
        auto revArea = sendArea.removeFromLeft(sendArea.getWidth() / 2);
        auto dlyArea = sendArea;
        auto revLabelArea = revArea.removeFromTop(10);
        auto dlyLabelArea = dlyArea.removeFromTop(10);
        localFaders[(size_t)i].setBounds(col);
        localPeakMeters[(size_t)i].setBounds(meterArea);
        localInputSelectors[(size_t)i].setBounds(inputArea);
        localInputModeSelectors[(size_t)i].setBounds(inputModeArea);
        localDbLabels[(size_t)i].setBounds(dbArea);
        localChannelNameLabels[(size_t)i].setBounds(nameArea);
        localReverbSendLabels[(size_t)i].setBounds(revLabelArea);
        localDelaySendLabels[(size_t)i].setBounds(dlyLabelArea);
        localReverbSendKnobs[(size_t)i].setBounds(revArea.reduced(2));
        localDelaySendKnobs[(size_t)i].setBounds(dlyArea.reduced(2));
    }

    masterFaderLabel.setBounds(masterArea.removeFromTop(20));
    auto masterInner = masterArea.reduced(4);
    auto masterMeterWidth = 10;
    auto masterMeterArea = masterInner.removeFromRight(masterMeterWidth);
    auto controlColumn = masterInner.removeFromLeft(70);
    auto fxColumn = masterInner;

    limiterButton.setBounds(controlColumn.removeFromTop(20));

    int bottomHeight = 70;
    if (bottomHeight > controlColumn.getHeight())
        bottomHeight = controlColumn.getHeight();

    auto threshArea = controlColumn.removeFromTop(controlColumn.getHeight() - bottomHeight);
    limiterThresholdSlider.setBounds(threshArea);

    auto releaseBlock = controlColumn;
    limiterReleaseLabel.setBounds(releaseBlock.removeFromTop(18));

    auto knobArea = releaseBlock.reduced(6, 0);
    int knobSize = juce::jmin(knobArea.getWidth(), knobArea.getHeight());
    juce::Rectangle<int> knobRect(0, 0, knobSize, knobSize);
    knobRect = knobRect.withCentre(knobArea.getCentre());
    limiterReleaseSlider.setBounds(knobRect);

    auto delayBlock = fxColumn.removeFromTop(70);
    delayTimeLabel.setBounds(delayBlock.removeFromTop(16));
    auto delayKnobBounds = delayBlock.reduced(4);
    int delayKnobSize = juce::jmin(delayKnobBounds.getWidth(), delayKnobBounds.getHeight());
    delayTimeSlider.setBounds(juce::Rectangle<int>(delayKnobSize, delayKnobSize).withCentre(delayKnobBounds.getCentre()));

    fxColumn.removeFromTop(2);
    delayDivisionSelector.setBounds(fxColumn.removeFromTop(22));
    fxColumn.removeFromTop(2);
    delayPingPongButton.setBounds(fxColumn.removeFromTop(22));

    masterFader.setBounds(masterInner.removeFromTop(masterInner.getHeight() - 16));
    masterDbLabel.setBounds(masterInner);
    masterPeakMeter.setBounds(masterMeterArea);

    if (showDockedChat)
    {
        chatDisplay.setVisible(true);
        chatInput.setVisible(true);
        sendButton.setVisible(true);
        atButton.setVisible(true);
        chatPopoutButton.setVisible(true);

        chatPopoutButton.setBounds(chatArea.removeFromTop(20).removeFromRight(70));

        auto chatInputArea = chatArea.removeFromBottom(30);
        chatArea.removeFromBottom(5);
        chatDisplay.setBounds(chatArea);

        sendButton.setBounds(chatInputArea.removeFromRight(60));
        chatInputArea.removeFromRight(5);
        atButton.setBounds(chatInputArea.removeFromRight(40));
        chatInputArea.removeFromRight(5);
        chatInput.setBounds(chatInputArea);
    }
    else
    {
        chatDisplay.setVisible(false);
        chatInput.setVisible(false);
        sendButton.setVisible(false);
        atButton.setVisible(false);
        chatPopoutButton.setVisible(false);
    }
}

void NinjamVst3AudioProcessorEditor::timerCallback()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    if (pendingDeferredResizeLayout && !audioProcessor.isStandaloneWrapper())
    {
        if (nowMs - lastResizeEventMs >= 85.0)
        {
            pendingDeferredResizeLayout = false;
            applyingDeferredResizeLayout = true;
            resized();
            applyingDeferredResizeLayout = false;
            suppressHeavyUiUntilMs = nowMs + 350.0;
            repaint();
        }
        else
        {
            return;
        }
    }

    int status = audioProcessor.getClient().GetStatus();
    updateHostResizeModeForConnectionStatus(status);
    juce::String statusStr;
    switch (status)
    {
        case NJClient::NJC_STATUS_DISCONNECTED: statusStr = "Disconnected"; break;
        case NJClient::NJC_STATUS_INVALIDAUTH:  statusStr = "Invalid Auth"; break;
        case NJClient::NJC_STATUS_CANTCONNECT:  statusStr = "Can't Connect"; break;
        case NJClient::NJC_STATUS_OK:           statusStr = "Connected"; break;
        case NJClient::NJC_STATUS_PRECONNECT:   statusStr = "Connecting..."; break;
        default: statusStr = "Unknown (" + juce::String(status) + ")"; break;
    }
    statusLabel.setText(statusStr, juce::dontSendNotification);

    if (status == NJClient::NJC_STATUS_OK || status == NJClient::NJC_STATUS_PRECONNECT)
        connectButton.setButtonText("Disconnect");
    else
        connectButton.setButtonText("Connect");

    if (shouldDeferHeavyUiWork())
        return;

    // Chat
    {
        const juce::ScopedTryLock lock(audioProcessor.chatLock);
        if (lock.isLocked())
        {
            const auto& history = audioProcessor.chatHistory;
            const auto& senders = audioProcessor.chatSenders;

            if (history.size() != lastChatSize)
            {
                applyColoredChat(chatDisplay, history, senders);
                lastChatSize = history.size();

                if (chatWindow)
                {
                    if (auto* popup = dynamic_cast<ChatPopupComponent*>(chatWindow->getContentComponent()))
                        popup->setChatText(history, senders);
                }
            }
        }
    }

    const bool heavyUiAllowed = nowMs >= suppressHeavyUiUntilMs;
    const bool runHeavyUiTick = ((++heavyUiTickCounter % 6) == 0);
    if (heavyUiAllowed && runHeavyUiTick)
    {
        userList.updateContent();
        syncUserStripMidiTargets();
        refreshMidiRelayTargetSelector();
    }
    applyMidiMappings();
    applyOscMappings();

    int numLocal = audioProcessor.getNumLocalChannels();
    numLocal = juce::jlimit(1, NinjamVst3AudioProcessor::maxLocalChannels, numLocal);
    for (int i = 0; i < numLocal; ++i)
    {
        float peak = audioProcessor.getLocalChannelPeak(i);
        localPeakMeters[(size_t)i].setPeak(peak);
        float db = -60.0f;
        if (peak > 1.0e-6f)
            db = juce::jlimit(-60.0f, 6.0f, 20.0f * std::log10(peak));
        localDbLabels[(size_t)i].setText(juce::String(db, 1) + " dB", juce::dontSendNotification);
    }

    float masterPk = audioProcessor.getMasterPeak();
    masterPeakMeter.setPeak(masterPk);
    {
        float db = -60.0f;
        if (masterPk > 1.0e-6f)
            db = juce::jlimit(-60.0f, 6.0f, 20.0f * std::log10(masterPk));
        masterDbLabel.setText(juce::String((int)std::round(db)) + " dB", juce::dontSendNotification);
    }

    if (autoLevelEnabled && runHeavyUiTick)
    {
        std::vector<NinjamVst3AudioProcessor::UserInfo> users = audioProcessor.getConnectedUsers();
        if (!users.empty())
        {
            const float timerIntervalMs = 50.0f;
            const float noiseFloor = 0.04f;
            const float baseTargetLevel = 0.4f;
            const float attackCoeff  = 1.0f - std::exp(-timerIntervalMs / 200.0f);
            const float releaseCoeff = 1.0f - std::exp(-timerIntervalMs / 1350.0f);
            const float longTermDecayCoeff = 1.0f - std::exp(-timerIntervalMs / 2000.0f);

            float masterPeak = audioProcessor.getMasterPeak();
            const float targetMasterLevel = 0.4f;
            float maxGain = 3.0f;
            if (masterPeak < 0.25f) maxGain = 4.0f;
            else if (masterPeak < 0.5f) maxGain = 3.5f;

            float globalGain = 1.0f;
            if (masterPeak > 0.0001f)
                globalGain = juce::jlimit(0.5f, 2.0f, targetMasterLevel / masterPeak);

            std::set<int> activeIds;

            for (auto& u : users)
            {
                int id = u.index;
                activeIds.insert(id);

                float peakL = audioProcessor.getUserPeak(id, 0);
                float peakR = audioProcessor.getUserPeak(id, 1);
                float currentLevel = juce::jmax(peakL, peakR);

                bool clipEnabled = audioProcessor.isUserClipEnabled(id);
                if (clipEnabled)
                {
                    auto softClipLevel = [](float x)
                    {
                        const float k = 2.0f;
                        const float d = std::tanh(k);
                        const float c = d / k;
                        const float target = 0.630957f;
                        float y = std::tanh(k * c * x);
                        if (d != 0.0f) y = (y / d) * target;
                        return y;
                    };
                    currentLevel = softClipLevel(currentLevel);
                }

                if (!autoLevelCurrentGains.count(id))       autoLevelCurrentGains[id] = u.volume;
                if (!autoLevelPeakLevels.count(id))         autoLevelPeakLevels[id] = 0.0f;
                if (!autoLevelChannelActiveTicks.count(id)) autoLevelChannelActiveTicks[id] = 0;
                else                                         autoLevelChannelActiveTicks[id]++;

                bool isNew = autoLevelChannelActiveTicks[id] < 40;
                float& longTermPeak = autoLevelPeakLevels[id];

                if (currentLevel >= noiseFloor)
                    longTermPeak += (currentLevel - longTermPeak) * longTermDecayCoeff;
                else if (longTermPeak > 0.0f)
                    longTermPeak -= longTermPeak * (longTermDecayCoeff * 0.5f);

                longTermPeak = juce::jlimit(0.0f, 1.0f, longTermPeak);

                if (longTermPeak < noiseFloor)
                {
                    autoLevelCurrentGains[id] += (1.0f - autoLevelCurrentGains[id]) * releaseCoeff;
                    audioProcessor.rememberUserVolume(id, autoLevelCurrentGains[id], u.name);
                    audioProcessor.setUserVolume(id, autoLevelCurrentGains[id]);
                    continue;
                }

                float targetGain = juce::jlimit(0.1f, maxGain, (baseTargetLevel / longTermPeak) * globalGain);

                float estimatedOutput = currentLevel * targetGain;
                if (!clipEnabled && estimatedOutput > 0.99f && currentLevel > noiseFloor)
                    targetGain = juce::jlimit(0.1f, maxGain, 0.95f / currentLevel);

                bool reducing = targetGain < autoLevelCurrentGains[id];
                float smoothingCoeff = reducing ? releaseCoeff : attackCoeff;
                if (isNew) smoothingCoeff *= 0.5f;

                autoLevelCurrentGains[id] += (targetGain - autoLevelCurrentGains[id]) * smoothingCoeff;
                autoLevelCurrentGains[id] = juce::jlimit(0.0f, maxGain, autoLevelCurrentGains[id]);

                audioProcessor.rememberUserVolume(id, autoLevelCurrentGains[id], u.name);
                audioProcessor.setUserVolume(id, autoLevelCurrentGains[id]);
            }

            for (auto it = autoLevelCurrentGains.begin(); it != autoLevelCurrentGains.end();)
            {
                if (!activeIds.count(it->first))
                {
                    int id = it->first;
                    autoLevelPeakLevels.erase(id);
                    autoLevelChannelActiveTicks.erase(id);
                    it = autoLevelCurrentGains.erase(it);
                }
                else { ++it; }
            }
        }
    }

    intervalDisplay.repaint();

    // Advance video background frame if active (Windows only)
#if JUCE_WINDOWS
    if (videoFrameReader != nullptr)
    {
        auto frame = videoFrameReader->getLatestFrame();
        if (frame.isValid())
        {
            backgroundImage = std::move(frame);
            repaint();
        }
    }
#endif

    updateVoiceChatButtonColor();

    double hostBpm = 0.0;
    bool hostPlaying = false;
    {
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (audioProcessor.getHostPosition(info))
        {
            hostBpm = info.bpm;
            hostPlaying = info.isPlaying;
        }
    }

    float njBpm = audioProcessor.getBPM();
    int bpi = audioProcessor.getBPI();

    juce::String text;
    text << "NJ " << juce::String(njBpm, 1) << " / " << bpi << " BPI";
    if (hostBpm > 0.0)
        text << " | Host " << juce::String(hostBpm, 1) << (hostPlaying ? " (Play)" : " (Stop)");

    int codecMode = audioProcessor.getCodecMode();
    juce::String codec;
    if (codecMode == 2)      codec = "Opus";
    else if (codecMode == 1) codec = "Vorbis+Opus";
    else                     codec = "Vorbis";
    text << " | Codec " << codec;

    tempoLabel.setText(text, juce::dontSendNotification);

    if (!delayTimeSlider.isMouseButtonDown())
        delayTimeSlider.setValue(audioProcessor.getFxDelayTimeMs(), juce::dontSendNotification);
    delayDivisionSelector.setSelectedId(audioProcessor.getFxDelayDivision(), juce::dontSendNotification);
    delayPingPongButton.setToggleState(audioProcessor.isFxDelayPingPong(), juce::dontSendNotification);
    updateFxButtonLabel();
    updateFxControlsVisibility();
}

void NinjamVst3AudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (!(event.mods.isPopupMenu() || event.mods.isRightButtonDown()))
        return;

    juce::Component* start = event.originalComponent != nullptr ? event.originalComponent : event.eventComponent;
    for (auto* c = start; c != nullptr; c = c->getParentComponent())
    {
        auto it = midiTargetsByComponent.find(c);
        if (it != midiTargetsByComponent.end())
        {
            showMidiLearnMenuForComponent(*c, event.getScreenPosition());
            return;
        }
    }
}

void NinjamVst3AudioProcessorEditor::registerMidiLearnTarget(juce::Component& component, const juce::String& targetId, bool isToggle)
{
    auto existing = midiTargetsByComponent.find(&component);
    if (existing != midiTargetsByComponent.end() && existing->second.id != targetId)
        midiTargetsById.erase(existing->second.id);

    MidiLearnTarget target;
    target.id = targetId;
    target.component = &component;
    target.isToggle = isToggle;

    midiTargetsByComponent[&component] = target;
    midiTargetsById[targetId] = target;
    component.addMouseListener(this, false);
}

void NinjamVst3AudioProcessorEditor::syncUserStripMidiTargets()
{
    std::vector<juce::Component*> componentsToRemove;
    for (auto it = midiTargetsById.begin(); it != midiTargetsById.end();)
    {
        if (it->first.startsWith("user."))
        {
            if (it->second.component != nullptr)
                componentsToRemove.push_back(it->second.component);
            it = midiTargetsById.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto* component : componentsToRemove)
    {
        if (component != nullptr)
            component->removeMouseListener(this);
        midiTargetsByComponent.erase(component);
    }

    auto strips = userList.getStripPointers();
    for (auto* strip : strips)
    {
        const int userIdx = strip->getUserIndex();
        const juce::String prefix = "user." + juce::String(userIdx) + ".";
        registerMidiLearnTarget(strip->getVolumeSlider(), prefix + "volume", false);
        registerMidiLearnTarget(strip->getPanSlider(), prefix + "pan", false);
        registerMidiLearnTarget(strip->getMuteButton(), prefix + "mute", true);
        registerMidiLearnTarget(strip->getSoloButton(), prefix + "solo", true);
        for (int i = 0; i < 8; ++i)
            registerMidiLearnTarget(strip->getChannelSlider(i), prefix + "channel." + juce::String(i + 1), false);
    }
}

void NinjamVst3AudioProcessorEditor::showMidiLearnMenuForComponent(juce::Component& component, juce::Point<int> screenPos)
{
    auto it = midiTargetsByComponent.find(&component);
    if (it == midiTargetsByComponent.end())
        return;

    const juce::String targetId = it->second.id;
    juce::PopupMenu menu;
    menu.addItem(3, "OSC Learn");
    menu.addItem(4, "OSC Forget", oscSourceByTargetId.find(targetId) != oscSourceByTargetId.end());
    menu.addSeparator();
    menu.addItem(1, "MIDI Learn");
    menu.addItem(2, "MIDI Forget", midiSourceByTargetId.find(targetId) != midiSourceByTargetId.end());
    menu.addSeparator();
    menu.addItem(10, "Save Learn Mappings");
    menu.addItem(11, "Load Learn Mappings");
    menu.addItem(12, "Forget All Learn Mappings");
    juce::Rectangle<int> popupAnchor(screenPos.x, screenPos.y, 1, 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&component).withTargetScreenArea(popupAnchor),
                       [this, targetId](int result)
                       {
                           if (result == 3)
                           {
                               oscLearnArmedTargetId = targetId;
                           }
                           else if (result == 4)
                           {
                               oscSourceByTargetId.erase(targetId);
                               if (oscLearnArmedTargetId == targetId)
                                   oscLearnArmedTargetId.clear();
                               syncLearnMappingsToProcessor();
                           }
                           else if (result == 1)
                           {
                               midiLearnArmedTargetId = targetId;
                           }
                           else if (result == 2)
                           {
                               midiSourceByTargetId.erase(targetId);
                               if (midiLearnArmedTargetId == targetId)
                                   midiLearnArmedTargetId.clear();
                               syncLearnMappingsToProcessor();
                           }
                           else if (result == 10)
                           {
                               saveLearnMappingsToDisk();
                           }
                           else if (result == 11)
                           {
                               loadLearnMappingsFromDisk();
                           }
                           else if (result == 12)
                           {
                               clearLearnMappings();
                           }
                       });
}

void NinjamVst3AudioProcessorEditor::applyMidiMappings()
{
    auto events = audioProcessor.popPendingMidiControllerEvents();
    if (events.empty())
        return;

    for (const auto& event : events)
    {
        if (midiLearnArmedTargetId.isNotEmpty() && midiTargetsById.find(midiLearnArmedTargetId) != midiTargetsById.end())
        {
            MidiSourceMapping mapping;
            mapping.isController = event.isController;
            mapping.midiChannel = event.midiChannel;
            mapping.number = event.number;
            mapping.lastBinaryState = event.isNoteOn ? 1 : 0;
            midiSourceByTargetId[midiLearnArmedTargetId] = mapping;
            midiLearnArmedTargetId.clear();
            syncLearnMappingsToProcessor();
        }

        for (auto& pair : midiSourceByTargetId)
        {
            auto targetIt = midiTargetsById.find(pair.first);
            if (targetIt == midiTargetsById.end())
                continue;

            auto& mapping = pair.second;
            if (mapping.isController != event.isController)
                continue;
            if (mapping.midiChannel != event.midiChannel || mapping.number != event.number)
                continue;

            auto* component = targetIt->second.component;
            if (component == nullptr)
                continue;

            if (targetIt->second.isToggle)
            {
                int binaryState = event.isNoteOn ? 1 : (event.value >= 64 ? 1 : 0);
                if (binaryState == 1 && mapping.lastBinaryState != 1)
                    if (auto* button = dynamic_cast<juce::Button*>(component))
                        button->triggerClick();
                mapping.lastBinaryState = binaryState;
            }
            else
            {
                if (auto* slider = dynamic_cast<juce::Slider*>(component))
                {
                    if (slider->isMouseButtonDown())
                        continue;
                    auto range = slider->getRange();
                    const double norm = juce::jlimit(0.0, 1.0, (double)event.normalized);
                    const double value = juce::jlimit(range.getStart(), range.getEnd(), range.getStart() + norm * range.getLength());
                    slider->setValue(value, juce::sendNotificationSync);
                }
            }
        }
    }
}

void NinjamVst3AudioProcessorEditor::applyOscMappings()
{
    std::vector<PendingOscEvent> events;
    {
        const juce::SpinLock::ScopedLockType lock(oscEventQueueLock);
        events.swap(pendingOscEvents);
    }
    if (events.empty())
        return;

    for (const auto& event : events)
    {
        if (oscLearnArmedTargetId.isNotEmpty() && midiTargetsById.find(oscLearnArmedTargetId) != midiTargetsById.end())
        {
            OscSourceMapping mapping;
            mapping.address = event.address;
            mapping.lastBinaryState = event.binaryOn ? 1 : 0;
            oscSourceByTargetId[oscLearnArmedTargetId] = mapping;
            oscLearnArmedTargetId.clear();
            syncLearnMappingsToProcessor();
        }

        for (auto& pair : oscSourceByTargetId)
        {
            auto targetIt = midiTargetsById.find(pair.first);
            if (targetIt == midiTargetsById.end())
                continue;
            auto* component = targetIt->second.component;
            if (component == nullptr)
                continue;
            auto& mapping = pair.second;
            if (mapping.address != event.address)
                continue;

            if (targetIt->second.isToggle)
            {
                const int binaryState = event.binaryOn ? 1 : 0;
                if (binaryState == 1 && mapping.lastBinaryState != 1)
                    if (auto* button = dynamic_cast<juce::Button*>(component))
                        button->triggerClick();
                mapping.lastBinaryState = binaryState;
            }
            else if (auto* slider = dynamic_cast<juce::Slider*>(component))
            {
                if (slider->isMouseButtonDown())
                    continue;
                auto range = slider->getRange();
                const double norm = juce::jlimit(0.0, 1.0, (double)event.normalized);
                const double value = juce::jlimit(range.getStart(), range.getEnd(), range.getStart() + norm * range.getLength());
                slider->setValue(value, juce::sendNotificationSync);
            }
        }
    }
}

void NinjamVst3AudioProcessorEditor::applyRemoteMidiRelaySelection(int channel, int inputIndex)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("channel", channel);
    obj->setProperty("inputIndex", inputIndex);
    const juce::String payload = juce::JSON::toString(juce::var(obj.get()));
    audioProcessor.sendSideSignal(audioProcessor.getMidiRelayTarget(), "localInputSelect", payload);
}

void NinjamVst3AudioProcessorEditor::syncLearnMappingsToProcessor()
{
    juce::Array<juce::var> midiArray;
    for (const auto& pair : midiSourceByTargetId)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("target", pair.first);
        obj->setProperty("isController", pair.second.isController);
        obj->setProperty("midiChannel", pair.second.midiChannel);
        obj->setProperty("number", pair.second.number);
        obj->setProperty("lastBinaryState", pair.second.lastBinaryState);
        midiArray.add(juce::var(obj.get()));
    }

    juce::Array<juce::var> oscArray;
    for (const auto& pair : oscSourceByTargetId)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("target", pair.first);
        obj->setProperty("address", pair.second.address);
        obj->setProperty("lastBinaryState", pair.second.lastBinaryState);
        oscArray.add(juce::var(obj.get()));
    }

    audioProcessor.setMidiLearnStateJson(juce::JSON::toString(juce::var(midiArray)));
    audioProcessor.setOscLearnStateJson(juce::JSON::toString(juce::var(oscArray)));
}

void NinjamVst3AudioProcessorEditor::loadLearnMappingsFromProcessor()
{
    midiSourceByTargetId.clear();
    oscSourceByTargetId.clear();

    const juce::var midiParsed = juce::JSON::parse(audioProcessor.getMidiLearnStateJson());
    if (auto* midiArray = midiParsed.getArray())
    {
        for (const auto& entry : *midiArray)
        {
            auto* obj = entry.getDynamicObject();
            if (obj == nullptr || !obj->hasProperty("target"))
                continue;
            MidiSourceMapping mapping;
            mapping.isController = obj->hasProperty("isController") ? (bool)obj->getProperty("isController") : true;
            mapping.midiChannel = obj->hasProperty("midiChannel") ? (int)obj->getProperty("midiChannel") : 1;
            mapping.number = obj->hasProperty("number") ? (int)obj->getProperty("number") : 0;
            mapping.lastBinaryState = obj->hasProperty("lastBinaryState") ? (int)obj->getProperty("lastBinaryState") : -1;
            midiSourceByTargetId[obj->getProperty("target").toString()] = mapping;
        }
    }

    const juce::var oscParsed = juce::JSON::parse(audioProcessor.getOscLearnStateJson());
    if (auto* oscArray = oscParsed.getArray())
    {
        for (const auto& entry : *oscArray)
        {
            auto* obj = entry.getDynamicObject();
            if (obj == nullptr || !obj->hasProperty("target") || !obj->hasProperty("address"))
                continue;
            OscSourceMapping mapping;
            mapping.address = obj->getProperty("address").toString();
            mapping.lastBinaryState = obj->hasProperty("lastBinaryState") ? (int)obj->getProperty("lastBinaryState") : -1;
            oscSourceByTargetId[obj->getProperty("target").toString()] = mapping;
        }
    }
}

void NinjamVst3AudioProcessorEditor::saveLearnMappingsToDisk()
{
    syncLearnMappingsToProcessor();
    juce::PropertiesFile::Options popts;
    popts.applicationName = "NINJAM VST3";
    popts.filenameSuffix = "settings";
    popts.folderName = "NINJAM VST3";
    popts.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile props(popts);
    props.setValue("midiLearnStateJson", audioProcessor.getMidiLearnStateJson());
    props.setValue("oscLearnStateJson", audioProcessor.getOscLearnStateJson());
    props.saveIfNeeded();
}

void NinjamVst3AudioProcessorEditor::loadLearnMappingsFromDisk()
{
    juce::PropertiesFile::Options popts;
    popts.applicationName = "NINJAM VST3";
    popts.filenameSuffix = "settings";
    popts.folderName = "NINJAM VST3";
    popts.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile props(popts);
    audioProcessor.setMidiLearnStateJson(props.getValue("midiLearnStateJson", {}));
    audioProcessor.setOscLearnStateJson(props.getValue("oscLearnStateJson", {}));
    loadLearnMappingsFromProcessor();
}

void NinjamVst3AudioProcessorEditor::clearLearnMappings()
{
    midiSourceByTargetId.clear();
    oscSourceByTargetId.clear();
    midiLearnArmedTargetId.clear();
    oscLearnArmedTargetId.clear();
    syncLearnMappingsToProcessor();
}

void NinjamVst3AudioProcessorEditor::connectClicked()
{
    const int status = audioProcessor.getClient().GetStatus();
    const bool isConnectedOrConnecting = (status == NJClient::NJC_STATUS_OK || status == NJClient::NJC_STATUS_PRECONNECT);
    if (!isConnectedOrConnecting)
    {
        juce::String user = userField.getText();
        juce::String pass = passField.getText();

        if (anonymousButton.getToggleState())
        {
            if (!user.startsWith("anonymous:"))
                user = "anonymous:" + user;
            pass = "";
        }

        audioProcessor.connectToServer(serverField.getText(), user, pass);
    }
    else
    {
        audioProcessor.disconnectFromServer();
        clearLearnMappings();
    }
}

void NinjamVst3AudioProcessorEditor::sendClicked()
{
    juce::String msg = chatInput.getText();
    if (msg.isNotEmpty())
    {
        audioProcessor.sendChatMessage(msg);
        chatInput.clear();
    }
}

void NinjamVst3AudioProcessorEditor::transmitToggled()
{
    audioProcessor.setTransmitLocal(transmitButton.getToggleState());
    updateTransmitButtonColor();
}

void NinjamVst3AudioProcessorEditor::layoutToggled()
{
    userList.setLayoutMode(layoutButton.getToggleState());
}

void NinjamVst3AudioProcessorEditor::metronomeChanged()
{
    // only update volume when not muted
    if (metronomeMuteButton.getToggleState())
        audioProcessor.setMetronomeVolume((float)metronomeSlider.getValue());
    else
        storedMetronomeVolume = (float)metronomeSlider.getValue(); // update stored value silently
}

void NinjamVst3AudioProcessorEditor::chatToggled()
{
    if (!chatButton.getToggleState())
    {
        if (chatWindow)
        {
            chatWindow->setVisible(false);
            chatWindow.reset();
        }
        chatPoppedOut = false;
    }
    updateChatButtonColor();
    resized();
}

void NinjamVst3AudioProcessorEditor::chatPopoutClicked()
{
    if (!chatButton.getToggleState())
    {
        chatButton.setToggleState(true, juce::dontSendNotification);
        updateChatButtonColor();
    }

    if (!chatPoppedOut)
    {
        chatPoppedOut = true;
        if (!chatWindow)
        {
            chatWindow.reset(new ChatWindow(audioProcessor, [this]()
            {
                chatWindow.reset();
                chatPoppedOut = false;
                chatButton.setToggleState(false, juce::dontSendNotification);
                updateChatButtonColor();
                resized();
            }));
        }
        else
        {
            chatWindow->setVisible(true);
        }
    }
    else
    {
        chatPoppedOut = false;
        if (chatWindow)
        {
            chatWindow->setVisible(false);
            chatWindow.reset();
        }
    }

    resized();
}

void NinjamVst3AudioProcessorEditor::anonymousToggled()
{
    passField.setEnabled(!anonymousButton.getToggleState());
}

void NinjamVst3AudioProcessorEditor::atToggled()
{
    audioProcessor.setAutoTranslateEnabled(atButton.getToggleState());
}

void NinjamVst3AudioProcessorEditor::syncToggled()
{
    bool enabled = syncButton.getToggleState();
    audioProcessor.setSyncToHost(enabled);

    if (!enabled)
        return;

    double hostBpm = 0.0;
    bool hostPlaying = false;
    {
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (audioProcessor.getHostPosition(info))
        {
            hostBpm = info.bpm;
            hostPlaying = info.isPlaying;
        }
    }

    float njBpm = audioProcessor.getBPM();
    juce::String message;
    bool anyWarning = false;

    if (hostBpm > 0.0 && njBpm > 0.0f)
    {
        double diff = std::abs(hostBpm - (double)njBpm);
        if (diff > 0.5)
        {
            anyWarning = true;
            message << "Host BPM (" << juce::String(hostBpm, 1)
                    << ") is different from NINJAM BPM ("
                    << juce::String(njBpm, 1) << ").\n";
        }
    }

    if (hostPlaying)
    {
        anyWarning = true;
        message << "The DAW is currently playing.\n"
                << "Stop the DAW, move the playhead to your desired start bar,\n"
                << "then press Play to hear NINJAM in sync with the host.";
    }

    if (!anyWarning)
        return;

    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Sync to Host", message);
}

void NinjamVst3AudioProcessorEditor::videoClicked()
{
    audioProcessor.launchVideoSession();
}

void NinjamVst3AudioProcessorEditor::serverListClicked()
{
    if (serverListWindow == nullptr)
    {
        serverListWindow.reset(new ServerListWindow(
            audioProcessor,
            [this](const juce::String& hostPort)
            {
                serverField.setText(hostPort, juce::dontSendNotification);
            },
            [this](const juce::String& hostPort)
            {
                serverField.setText(hostPort, juce::dontSendNotification);
                if (audioProcessor.getClient().GetStatus() != NJClient::NJC_STATUS_DISCONNECTED)
                    audioProcessor.disconnectFromServer();

                juce::String user = userField.getText();
                juce::String pass = passField.getText();
                if (anonymousButton.getToggleState())
                {
                    if (!user.startsWith("anonymous:"))
                        user = "anonymous:" + user;
                    pass = "";
                }

                audioProcessor.connectToServer(serverField.getText(), user, pass);
                if (serverListWindow != nullptr)
                    serverListWindow->setVisible(false);
            }));
    }
    else
    {
        serverListWindow->setVisible(true);
        serverListWindow->toFront(true);
    }
}

void NinjamVst3AudioProcessorEditor::refreshLocalInputSelectors()
{
    for (int i = 0; i < NinjamVst3AudioProcessor::maxLocalChannels; ++i)
        refreshLocalInputSelector(i);
}

void NinjamVst3AudioProcessorEditor::refreshMidiRelayTargetSelector()
{
    const juce::String selectedTarget = audioProcessor.getMidiRelayTarget();
    std::set<juce::String> seen;
    midiRelayTargetByMenuId.clear();
    midiRelayTargetSelector.clear(juce::dontSendNotification);

    int id = 1;
    midiRelayTargetSelector.addItem("MIDI->All", id);
    midiRelayTargetByMenuId[id] = "*";
    ++id;

    for (const auto& user : audioProcessor.getConnectedUsers())
    {
        if (user.name.isEmpty() || seen.find(user.name) != seen.end())
            continue;
        seen.insert(user.name);
        midiRelayTargetSelector.addItem("MIDI->" + user.name, id);
        midiRelayTargetByMenuId[id] = user.name;
        ++id;
    }

    int selectedId = 1;
    for (const auto& pair : midiRelayTargetByMenuId)
        if (pair.second.equalsIgnoreCase(selectedTarget))
            selectedId = pair.first;

    midiRelayTargetSelector.setSelectedId(selectedId, juce::dontSendNotification);
}

void NinjamVst3AudioProcessorEditor::oscMessageReceived(const juce::OSCMessage& message)
{
    PendingOscEvent event;
    event.address = message.getAddressPattern().toString();
    if (message.size() > 0)
    {
        const auto arg = message[0];
        float raw = 1.0f;
        if (arg.isFloat32()) raw = arg.getFloat32();
        else if (arg.isInt32()) raw = (float)arg.getInt32();
        event.normalized = raw > 1.0f ? juce::jlimit(0.0f, 1.0f, raw / 127.0f) : juce::jlimit(0.0f, 1.0f, raw);
        event.binaryOn = raw >= 0.5f;
    }
    else
    {
        event.normalized = 1.0f;
        event.binaryOn = true;
    }

    const juce::SpinLock::ScopedLockType lock(oscEventQueueLock);
    pendingOscEvents.push_back(event);
    if (pendingOscEvents.size() > 512)
        pendingOscEvents.erase(pendingOscEvents.begin(), pendingOscEvents.begin() + (long long)(pendingOscEvents.size() - 512));
}

void NinjamVst3AudioProcessorEditor::refreshLocalInputSelector(int channel)
{
    if (channel < 0 || channel >= NinjamVst3AudioProcessor::maxLocalChannels)
        return;

    auto& selector = localInputSelectors[(size_t)channel];
    selector.clear(juce::dontSendNotification);

    int total = audioProcessor.getTotalNumInputChannels();
    if (total <= 0) total = 2;
    int numPairs = total / 2;

    for (int ch = 0; ch < total; ++ch)
        selector.addItem("In " + juce::String(ch + 1), ch + 1);

    int stereoBaseId = 100;
    for (int pair = 0; pair < numPairs; ++pair)
    {
        int left = pair * 2 + 1;
        int right = left + 1;
        selector.addItem(juce::String(left) + "/" + juce::String(right), stereoBaseId + pair);
    }

    int currentInput = audioProcessor.getLocalChannelInput(channel);
    if (currentInput >= 0 && currentInput < total)
    {
        selector.setSelectedId(currentInput + 1, juce::dontSendNotification);
    }
    else if (currentInput < 0)
    {
        int pairIndex = -1 - currentInput;
        if (numPairs > pairIndex)
        {
            selector.setSelectedId(stereoBaseId + pairIndex, juce::dontSendNotification);
        }
        else if (numPairs > 0)
        {
            selector.setSelectedId(stereoBaseId, juce::dontSendNotification);
            audioProcessor.setLocalChannelInput(channel, -1);
        }
        else if (total > 0)
        {
            selector.setSelectedId(1, juce::dontSendNotification);
            audioProcessor.setLocalChannelInput(channel, 0);
        }
    }

    if (channel >= 0 && channel < NinjamVst3AudioProcessor::maxLocalChannels)
        localInputModeSelectors[(size_t)channel].setSelectedId(currentInput < 0 ? 2 : 1, juce::dontSendNotification);
}

bool NinjamVst3AudioProcessorEditor::isSidechainInputActive() const
{
    return audioProcessor.getTotalNumInputChannels() > 2;
}

void NinjamVst3AudioProcessorEditor::loadControlImages(const juce::File& themeDir)
{
    backgroundImage = juce::Image();

    // Try bg.mp4 when the Video BG toggle is on (Windows only)
    bool videoLoaded = false;

#if JUCE_WINDOWS
    videoFrameReader.reset();
    if (videoBgToggle.getToggleState())
    {
        auto videoFile = themeDir.getChildFile("bg.mp4");
        if (videoFile.existsAsFile())
        {
            videoFrameReader = std::make_unique<WinVideoReader>();
            if (videoFrameReader->open(videoFile))
                videoLoaded = true;
            else
                videoFrameReader.reset();
        }
    }
#endif

    if (!videoLoaded)
    {
        // Fall back to bg.* image files (bg.jpg, bg.png, bg.gif, etc.)
        auto bgFiles = themeDir.findChildFiles(juce::File::findFiles, false, "bg.*");
        if (!bgFiles.isEmpty())
            backgroundImage = juce::ImageFileFormat::loadFrom(bgFiles[0]);
    }

    // fknob.png — fader knob image
    faderKnobImage = juce::ImageFileFormat::loadFrom(themeDir.getChildFile("fknob.png"));

    // rknob.png — radio/release knob image
    radioKnobImage = juce::ImageFileFormat::loadFrom(themeDir.getChildFile("rknob.png"));

    // Reset theme colours to defaults before reading cfg
    metronomeThemeColour = juce::Colour::fromRGB(80, 185, 255);
    windowThemeColour    = juce::Colour(0x00000000);
    buttonThemeColour    = juce::Colour(0x00000000);
    menuBarThemeColour   = juce::Colour(0x00000000);
    knobColourPreset     = "grey";
    faderColourPreset    = "grey";
    knobThemeColour      = juce::Colours::grey;
    faderThemeColour     = juce::Colour(0xff666666);

    // Parse companion skin.cfg if present
    auto cfgFile = themeDir.getChildFile("skin.cfg");
    if (cfgFile.existsAsFile())
    {
        auto lines = juce::StringArray::fromLines(cfgFile.loadFileAsString());
        auto parseHex = [](const juce::String& val, juce::Colour& out) -> bool
        {
            auto s = val.trim().trimCharactersAtStart("#");
            if (s.length() == 6 && s.containsOnly("0123456789abcdefABCDEF"))
            {
                out = juce::Colour::fromString("ff" + s);
                return true;
            }
            return false;
        };
        for (const auto& line : lines)
        {
            auto trimmed = line.trim();
            if (trimmed.startsWith("#") || trimmed.isEmpty()) continue;
            auto val     = trimmed.fromFirstOccurrenceOf(":", false, false).trim();
            if (trimmed.startsWithIgnoreCase("Metronome Colour:"))
                parseHex(val, metronomeThemeColour);
            else if (trimmed.startsWithIgnoreCase("Window Colour:"))
                parseHex(val, windowThemeColour);
            else if (trimmed.startsWithIgnoreCase("Button Colour:"))
                parseHex(val, buttonThemeColour);
            else if (trimmed.startsWithIgnoreCase("MenuBar Colour:"))
                parseHex(val, menuBarThemeColour);
            else if (trimmed.startsWithIgnoreCase("Knobs:"))
            {
                knobColourPreset = val;
                knobThemeColour = colourFromPresetName(knobColourPreset, juce::Colours::grey);
            }
            else if (trimmed.startsWithIgnoreCase("Faders:"))
            {
                faderColourPreset = val;
                faderThemeColour = colourFromPresetName(faderColourPreset, juce::Colour(0xff666666));
            }
        }
    }

    applyThemeColours();
    repaint();
}

void NinjamVst3AudioProcessorEditor::applyThemeColours()
{
    metronomeBtnLAF.themeColour = metronomeThemeColour;
    metronomeMuteButton.repaint();

    // Apply Window Colour to the global LAF palette so all component backgrounds pick it up.
    // If no Window Colour is set, restore JUCE defaults.
    if (windowThemeColour.getAlpha() > 0)
    {
        auto bg   = windowThemeColour;
        auto bgDk = bg.darker(0.25f);
        auto bgLt = bg.brighter(0.15f);

        // drawDocumentWindowTitleBar reads widgetBackground from the colour scheme directly
        // (bypasses colour IDs entirely), so we must patch the scheme to change the title bar.
        auto titleBarBg = menuBarThemeColour.getAlpha() > 0 ? menuBarThemeColour : bg;
        auto scheme = outlinedLabelLAF.getCurrentColourScheme();
        scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::windowBackground, titleBarBg);
        scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::widgetBackground, titleBarBg);
        outlinedLabelLAF.setColourScheme(scheme); // also calls initialiseColours(), resetting colour IDs

        // Now apply per-component colour ID overrides (override what setColourScheme just initialised)
        outlinedLabelLAF.setColour(juce::ResizableWindow::backgroundColourId,     bg);
        outlinedLabelLAF.setColour(juce::DocumentWindow::backgroundColourId,       bg);
        outlinedLabelLAF.setColour(juce::ComboBox::backgroundColourId,             bgDk);
        outlinedLabelLAF.setColour(juce::ListBox::backgroundColourId,              bgDk);
        outlinedLabelLAF.setColour(juce::TextEditor::backgroundColourId,           bgDk);
        outlinedLabelLAF.setColour(juce::TextButton::buttonColourId,
            buttonThemeColour.getAlpha() > 0 ? buttonThemeColour : bgLt);
        outlinedLabelLAF.setColour(juce::Slider::backgroundColourId,               bgDk);
        outlinedLabelLAF.setColour(juce::GroupComponent::outlineColourId,          bg.brighter(0.3f));
        outlinedLabelLAF.setColour(juce::PopupMenu::backgroundColourId,            bgDk);
        outlinedLabelLAF.setColour(juce::PopupMenu::highlightedBackgroundColourId, bgLt);
    }
    else
    {
        // Restore the dark colour scheme (JUCE default); this also resets all colour IDs.
        // If a MenuBar Colour is set without a Window Colour, still patch widgetBackground.
        if (menuBarThemeColour.getAlpha() > 0)
        {
            auto scheme = juce::LookAndFeel_V4::getDarkColourScheme();
            scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::windowBackground, menuBarThemeColour);
            scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::widgetBackground, menuBarThemeColour);
            outlinedLabelLAF.setColourScheme(scheme);
        }
        else
        {
            outlinedLabelLAF.setColourScheme(juce::LookAndFeel_V4::getDarkColourScheme());
        }
        if (buttonThemeColour.getAlpha() > 0)
            outlinedLabelLAF.setColour(juce::TextButton::buttonColourId, buttonThemeColour);
    }

    repaint();
    sendLookAndFeelChange();

    // Force the DocumentWindow title bar to repaint using the updated scheme.
    if (auto* dw = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
    {
        auto effectiveTitleCol = menuBarThemeColour.getAlpha() > 0 ? menuBarThemeColour
                               : windowThemeColour.getAlpha()    > 0 ? windowThemeColour
                               : juce::LookAndFeel_V4::getDarkColourScheme()
                                   .getUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::windowBackground);
        dw->setBackgroundColour(effectiveTitleCol);
        dw->repaint();
    }
}

void NinjamVst3AudioProcessorEditor::parentHierarchyChanged()
{
    // Re-apply title bar colour now that we may be properly parented under the DocumentWindow
    if (auto* dw = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
    {
        auto effectiveTitleCol = menuBarThemeColour.getAlpha() > 0 ? menuBarThemeColour
                               : windowThemeColour.getAlpha()    > 0 ? windowThemeColour
                               : juce::LookAndFeel_V4::getDarkColourScheme()
                                   .getUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::windowBackground);
        dw->setBackgroundColour(effectiveTitleCol);
        dw->repaint();
    }
}

bool NinjamVst3AudioProcessorEditor::shouldDeferHeavyUiWork() const
{
    if (audioProcessor.isStandaloneWrapper())
        return false;
    if (pendingDeferredResizeLayout || applyingDeferredResizeLayout)
        return true;
    return juce::Time::getMillisecondCounterHiRes() < suppressHeavyUiUntilMs;
}

bool NinjamVst3AudioProcessorEditor::isAbletonLiveHost() const
{
    return juce::PluginHostType().isAbletonLive();
}

void NinjamVst3AudioProcessorEditor::setAbletonWindowSizePreset(int presetIndex)
{
    if (audioProcessor.isStandaloneWrapper() || !isAbletonLiveHost())
        return;

    abletonWindowSizePreset = juce::jlimit(0, 2, presetIndex);

    int targetWidth = 1240;
    int targetHeight = 600;
    if (abletonWindowSizePreset == 0) targetWidth = 1100;
    if (abletonWindowSizePreset == 0) targetHeight = 540;
    if (abletonWindowSizePreset == 2) targetWidth = 1380;
    if (abletonWindowSizePreset == 2) targetHeight = 700;

    if (hostResizeLockedForConnection)
    {
        pendingDeferredResizeLayout = false;
        applyingDeferredResizeLayout = false;
        setResizable(false, false);
        setResizeLimits(targetWidth, targetHeight, targetWidth, targetHeight);
        setSize(targetWidth, targetHeight);
        suppressHeavyUiUntilMs = juce::Time::getMillisecondCounterHiRes() + 400.0;
    }
    else
    {
        setResizable(true, true);
        setResizeLimits(900, 500, 2200, 1500);
        setSize(targetWidth, juce::jlimit(500, 1500, targetHeight));
    }

    juce::PropertiesFile::Options popts;
    popts.applicationName     = "NINJAM VST3";
    popts.filenameSuffix      = "settings";
    popts.folderName          = "NINJAM VST3";
    popts.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile props(popts);
    props.setValue("abletonWindowSizePreset", abletonWindowSizePreset);
    props.saveIfNeeded();
}

void NinjamVst3AudioProcessorEditor::updateHostResizeModeForConnectionStatus(int status)
{
    if (audioProcessor.isStandaloneWrapper())
        return;

    const bool shouldLock = isAbletonLiveHost()
        && (status == NJClient::NJC_STATUS_OK || status == NJClient::NJC_STATUS_PRECONNECT);
    if (shouldLock == hostResizeLockedForConnection)
        return;

    if (shouldLock)
    {
        const int currentWidth = getWidth();
        const int currentHeight = getHeight();
        pendingDeferredResizeLayout = false;
        applyingDeferredResizeLayout = false;
        setResizable(false, false);
        setResizeLimits(currentWidth, currentHeight, currentWidth, currentHeight);
        suppressHeavyUiUntilMs = juce::Time::getMillisecondCounterHiRes() + 500.0;
    }
    else
    {
        setResizable(true, true);
        setResizeLimits(900, 500, 2200, 1500);
    }

    hostResizeLockedForConnection = shouldLock;
}

void NinjamVst3AudioProcessorEditor::updateAutoLevelButtonColor()
{
    if (autoLevelButton.getToggleState())
    {
        juce::Colour on = juce::Colour::fromRGB(240, 220, 30);   // bright yellow
        autoLevelButton.setColour(juce::TextButton::buttonColourId,   on);
        autoLevelButton.setColour(juce::TextButton::buttonOnColourId, on);
        autoLevelButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
        autoLevelButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    }
    else
    {
        juce::Colour off = juce::Colour::fromRGB(55, 50, 10);    // dim yellow
        autoLevelButton.setColour(juce::TextButton::buttonColourId,   off);
        autoLevelButton.setColour(juce::TextButton::buttonOnColourId, off);
        autoLevelButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::grey);
        autoLevelButton.setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    }
    repaint();
}

void NinjamVst3AudioProcessorEditor::updateChatButtonColor()
{
    chatButton.repaint();
}

void NinjamVst3AudioProcessorEditor::updateTransmitButtonColor()
{
    if (transmitButton.getToggleState())
    {
        juce::Colour on = juce::Colour::fromRGB(50, 200, 80);    // bright green when transmitting
        transmitButton.setColour(juce::TextButton::buttonColourId,   on);
        transmitButton.setColour(juce::TextButton::buttonOnColourId, on);
        transmitButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
        transmitButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    }
    else
    {
        juce::Colour off = juce::Colour::fromRGB(12, 50, 18);    // dim green
        transmitButton.setColour(juce::TextButton::buttonColourId,   off);
        transmitButton.setColour(juce::TextButton::buttonOnColourId, off);
        transmitButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::grey);
        transmitButton.setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    }
    repaint();
}

void NinjamVst3AudioProcessorEditor::updateMonitorButtonColor()
{
    if (localMonitorButton.getToggleState())
    {
        juce::Colour on = juce::Colour::fromRGB(220, 55, 55);    // bright red when monitoring
        localMonitorButton.setColour(juce::TextButton::buttonColourId,   on);
        localMonitorButton.setColour(juce::TextButton::buttonOnColourId, on);
        localMonitorButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
        localMonitorButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }
    else
    {
        juce::Colour off = juce::Colour::fromRGB(60, 15, 15);    // dim red
        localMonitorButton.setColour(juce::TextButton::buttonColourId,   off);
        localMonitorButton.setColour(juce::TextButton::buttonOnColourId, off);
        localMonitorButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::grey);
        localMonitorButton.setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    }
    repaint();
}

void NinjamVst3AudioProcessorEditor::updateLimiterButtonColor()
{
    if (limiterButton.getToggleState())
    {
        juce::Colour on = juce::Colour::fromRGB(220, 55, 55);    // bright red when active
        limiterButton.setColour(juce::TextButton::buttonColourId,   on);
        limiterButton.setColour(juce::TextButton::buttonOnColourId, on);
        limiterButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
        limiterButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }
    else
    {
        juce::Colour off = juce::Colour::fromRGB(60, 15, 15);    // dim red
        limiterButton.setColour(juce::TextButton::buttonColourId,   off);
        limiterButton.setColour(juce::TextButton::buttonOnColourId, off);
        limiterButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::grey);
        limiterButton.setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    }
    repaint();
}

void NinjamVst3AudioProcessorEditor::updateVoiceChatButtonColor()
{
    if (voiceChatButton.getToggleState())
    {
        // Pulse between dim amber and bright amber (~1 s cycle)
        voiceChatGlowPhase += juce::MathConstants<float>::twoPi * 30.0f / 1000.0f;
        float t = (std::sin(voiceChatGlowPhase) + 1.0f) * 0.5f; // 0..1
        uint8 r = (uint8)(100 + (uint8)(155 * t));
        uint8 g = (uint8)(50  + (uint8)(100 * t));
        juce::Colour pulse = juce::Colour::fromRGB(r, g, 0);
        voiceChatButton.setColour(juce::TextButton::buttonColourId,   pulse);
        voiceChatButton.setColour(juce::TextButton::buttonOnColourId, pulse);
        voiceChatButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
        voiceChatButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    }
    else
    {
        voiceChatGlowPhase = 0.0f;
        juce::Colour off = juce::Colour::fromRGB(50, 30, 0);
        voiceChatButton.setColour(juce::TextButton::buttonColourId,   off);
        voiceChatButton.setColour(juce::TextButton::buttonOnColourId, off);
        voiceChatButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::orange);
        voiceChatButton.setColour(juce::TextButton::textColourOffId, juce::Colours::orange);
    }
}

void NinjamVst3AudioProcessorEditor::updateLayoutButtonColor()
{
    repaint();
}

void NinjamVst3AudioProcessorEditor::updateMetronomeButtonColor()
{
    repaint();
}

void NinjamVst3AudioProcessorEditor::updateSyncButtonColor()
{
    repaint();
}

void NinjamVst3AudioProcessorEditor::updateFxButtonLabel()
{
    fxButton.setButtonText("FX");
}

void NinjamVst3AudioProcessorEditor::showFxMenu()
{
    audioProcessor.setFxReverbEnabled(true);
    audioProcessor.setFxDelayEnabled(true);

    juce::PopupMenu menu;
    menu.addItem(2, "Reverb");
    menu.addItem(3, "Delay");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fxButton),
        [this](int result)
        {
            if (result == 0)
                return;

            audioProcessor.setFxReverbEnabled(true);
            audioProcessor.setFxDelayEnabled(true);
            if (result == 2)
                showReverbSettingsPopup();
            if (result == 3)
                showDelaySettingsPopup();
            updateFxButtonLabel();
            updateFxControlsVisibility();
            repaint();
        });
}

void NinjamVst3AudioProcessorEditor::showOptionsMenu()
{
    juce::PopupMenu menu;
    menu.addItem(41, "Midi Settings");
    if (isAbletonLiveHost() && !audioProcessor.isStandaloneWrapper())
    {
        juce::PopupMenu sizeMenu;
        sizeMenu.addItem(51, "Small", true, abletonWindowSizePreset == 0);
        sizeMenu.addItem(52, "Medium", true, abletonWindowSizePreset == 1);
        sizeMenu.addItem(53, "Large", true, abletonWindowSizePreset == 2);
        menu.addSubMenu("Window Size", sizeMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&optionsButton),
        [this](int result)
        {
            if (result == 0)
                return;
            if (result == 41)
                showMidiOptionsPopup();
            if (result == 51) setAbletonWindowSizePreset(0);
            if (result == 52) setAbletonWindowSizePreset(1);
            if (result == 53) setAbletonWindowSizePreset(2);
        });
}

void NinjamVst3AudioProcessorEditor::showReverbSettingsPopup()
{
    showSettingsCallout(std::make_unique<ReverbSettingsPopupComponent>(audioProcessor), fxButton);
}

void NinjamVst3AudioProcessorEditor::showDelaySettingsPopup()
{
    showSettingsCallout(std::make_unique<DelaySettingsPopupComponent>(audioProcessor), fxButton);
}

void NinjamVst3AudioProcessorEditor::showMidiOptionsPopup()
{
    showSettingsCallout(std::make_unique<MidiOptionsPopupComponent>(audioProcessor, [this] { refreshExternalMidiInputDevices(); }),
                        optionsButton.isShowing() ? static_cast<juce::Component&>(optionsButton)
                                                  : static_cast<juce::Component&>(fxButton));
}

void NinjamVst3AudioProcessorEditor::showSettingsCallout(std::unique_ptr<juce::Component> content, juce::Component& anchorComponent)
{
    auto anchorOnScreen = anchorComponent.getScreenBounds();
    juce::Rectangle<int> target(anchorOnScreen.getX() + 8, anchorOnScreen.getBottom() + 2, 2, 2);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(content), target, nullptr);
    box.setLookAndFeel(&noArrowCallOutLookAndFeel);
    box.setArrowSize(0.0f);
    box.setTopLeftPosition(anchorOnScreen.getX(), anchorOnScreen.getBottom() + 4);
}

void NinjamVst3AudioProcessorEditor::refreshExternalMidiInputDevices()
{
    const juce::String desiredLearnId = audioProcessor.getMidiLearnInputDeviceId();
    const juce::String desiredRelayId = audioProcessor.getMidiRelayInputDeviceId();

    if (desiredLearnId != openedMidiLearnInputDeviceId)
    {
        midiLearnInputDevice.reset();
        openedMidiLearnInputDeviceId.clear();
        if (desiredLearnId.isNotEmpty())
        {
            midiLearnInputDevice = juce::MidiInput::openDevice(desiredLearnId, this);
            if (midiLearnInputDevice != nullptr)
            {
                midiLearnInputDevice->start();
                openedMidiLearnInputDeviceId = desiredLearnId;
            }
        }
    }

    if (desiredRelayId == openedMidiLearnInputDeviceId && desiredRelayId.isNotEmpty())
    {
        midiRelayInputDevice.reset();
        openedMidiRelayInputDeviceId = desiredRelayId;
        return;
    }

    if (desiredRelayId != openedMidiRelayInputDeviceId)
    {
        midiRelayInputDevice.reset();
        openedMidiRelayInputDeviceId.clear();
        if (desiredRelayId.isNotEmpty())
        {
            midiRelayInputDevice = juce::MidiInput::openDevice(desiredRelayId, this);
            if (midiRelayInputDevice != nullptr)
            {
                midiRelayInputDevice->start();
                openedMidiRelayInputDeviceId = desiredRelayId;
            }
        }
    }
}

void NinjamVst3AudioProcessorEditor::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    if (source == nullptr)
        return;

    const juce::String sourceId = source->getIdentifier();
    const juce::String learnDeviceId = audioProcessor.getMidiLearnInputDeviceId();
    const juce::String relayDeviceId = audioProcessor.getMidiRelayInputDeviceId();
    const bool forLearn = learnDeviceId.isNotEmpty() && sourceId == learnDeviceId;
    const bool forRelay = relayDeviceId.isNotEmpty() && sourceId == relayDeviceId;
    if (!forLearn && !forRelay)
        return;

    NinjamVst3AudioProcessor::MidiControllerEvent event;
    if (message.isController())
    {
        event.isController = true;
        event.midiChannel = message.getChannel();
        event.number = message.getControllerNumber();
        event.value = message.getControllerValue();
        event.normalized = (float)event.value / 127.0f;
        event.isNoteOn = event.value >= 64;
    }
    else if (message.isNoteOnOrOff())
    {
        event.isController = false;
        event.midiChannel = message.getChannel();
        event.number = message.getNoteNumber();
        event.value = message.getVelocity();
        event.normalized = message.isNoteOn() ? ((float)event.value / 127.0f) : 0.0f;
        event.isNoteOn = message.isNoteOn();
    }
    else
    {
        return;
    }

    audioProcessor.enqueueExternalMidiControllerEvent(event, forLearn, forRelay);
}

void NinjamVst3AudioProcessorEditor::updateFxControlsVisibility()
{
    reverbRoomLabel.setVisible(false);
    reverbRoomSlider.setVisible(false);
    delayTimeLabel.setVisible(false);
    delayTimeSlider.setVisible(false);
    delayDivisionSelector.setVisible(false);
    delayPingPongButton.setVisible(false);
}

// ==============================================================================
// UserChannelStrip Implementation
// ==============================================================================

UserChannelStrip::UserChannelStrip(NinjamVst3AudioProcessor& p, int userIdx)
    : processor(p), userIndex(userIdx)
{
    // Initialise per-channel state
    for (int i = 0; i < kMaxRemoteCh; ++i)
    {
        perChannelGain[i] = 1.0f;
        channelPeaks[i]   = 0.0f;
    }

    setOpaque(false);
    addAndMakeVisible(nameLabel);
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange(0.0, 2.0);
    volumeSlider.setSkewFactorFromMidPoint(0.25);
    volumeSlider.setValue(1.0, juce::dontSendNotification);
    volumeSlider.setDoubleClickReturnValue(true, 1.0);
    volumeSlider.setLookAndFeel(&faderLookAndFeel);
    volumeSlider.onValueChange = [this] { volumeChanged(); };

    addAndMakeVisible(panSlider);
    panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange(-1.0, 1.0);
    panSlider.setValue(0.0, juce::dontSendNotification);
    panSlider.setDoubleClickReturnValue(true, 0.0);
    panSlider.onValueChange = [this] { panChanged(); };

    addAndMakeVisible(muteButton);
    muteButton.setClickingTogglesState(true);
    muteBtnLAF.isMute = true;
    muteButton.setLookAndFeel(&muteBtnLAF);
    muteButton.onClick = [this] { muteChanged(); };

    addAndMakeVisible(soloButton);
    soloButton.setClickingTogglesState(true);
    soloBtnLAF.isMute = false;
    soloButton.setLookAndFeel(&soloBtnLAF);
    soloButton.onClick = [this] { soloChanged(); };

    addAndMakeVisible(outputSelector);

    addAndMakeVisible(dbLabel);
    dbLabel.setJustificationType(juce::Justification::centred);
    dbLabel.setColour(juce::Label::backgroundColourId, juce::Colours::black);
    dbLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    dbLabel.setFont(juce::Font(11.0f));

    int totalOutputs = processor.getTotalNumOutputChannels();
    if (totalOutputs <= 0) totalOutputs = 2;
    int numPairs = totalOutputs / 2;

    for (int ch = 0; ch < totalOutputs; ++ch)
        outputSelector.addItem("Out " + juce::String(ch + 1), ch + 1);

    int stereoBaseId = 100;
    for (int pair = 0; pair < numPairs; ++pair)
    {
        int left = pair * 2 + 1;
        int right = left + 1;
        outputSelector.addItem("Out " + juce::String(left) + "/" + juce::String(right),
                               stereoBaseId + pair);
    }

    outputSelector.onChange = [this] { outputChanged(); };

    // Expand button — shows ">" in list layout for multichan peers
    expandButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    expandButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addChildComponent(expandButton); // hidden until isMultiChanPeer
    expandButton.onClick = [this] { toggleExpanded(); };

    // Per-channel volume sliders and name labels (hidden until expanded)
    for (int i = 0; i < kMaxRemoteCh; ++i)
    {
        channelSliders[i].setSliderStyle(juce::Slider::LinearHorizontal);
        channelSliders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        channelSliders[i].setRange(0.0, 2.0);
        channelSliders[i].setSkewFactorFromMidPoint(0.25);
        channelSliders[i].setValue(1.0, juce::dontSendNotification);
        channelSliders[i].setDoubleClickReturnValue(true, 1.0);
        channelSliders[i].setLookAndFeel(&faderLookAndFeel);
        int ch = i;
        channelSliders[i].onValueChange = [this, ch]
        {
            perChannelGain[ch] = (float)channelSliders[ch].getValue();
            float master = (float)volumeSlider.getValue();
            // NINJAM ch0 = Vorbis mixdown; individual channels start at ch1
            processor.setUserNjChannelVolume(userIndex, ch + 1, master * perChannelGain[ch]);
        };
        addChildComponent(channelSliders[i]);

        channelNameLabels[i].setFont(juce::Font(9.0f));
        channelNameLabels[i].setJustificationType(juce::Justification::centredLeft);
        channelNameLabels[i].setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addChildComponent(channelNameLabels[i]);
    }

    startTimer(50);
}

UserChannelStrip::~UserChannelStrip()
{
    volumeSlider.setLookAndFeel(nullptr);
    panSlider.setLookAndFeel(nullptr);
    muteButton.setLookAndFeel(nullptr);
    soloButton.setLookAndFeel(nullptr);
    for (int i = 0; i < kMaxRemoteCh; ++i)
        channelSliders[i].setLookAndFeel(nullptr);
    stopTimer();
}

int UserChannelStrip::getUserIndex() const
{
    return userIndex;
}

juce::Slider& UserChannelStrip::getVolumeSlider()
{
    return volumeSlider;
}

juce::Slider& UserChannelStrip::getPanSlider()
{
    return panSlider;
}

juce::Button& UserChannelStrip::getMuteButton()
{
    return muteButton;
}

juce::Button& UserChannelStrip::getSoloButton()
{
    return soloButton;
}

juce::Slider& UserChannelStrip::getChannelSlider(int channel)
{
    return channelSliders[(size_t)juce::jlimit(0, kMaxRemoteCh - 1, channel)];
}

void UserChannelStrip::paintOverChildren(juce::Graphics& g)
{
    auto drawGlow = [&](juce::Button& btn, juce::Colour onColour, juce::Colour offColour)
    {
        bool isOn = btn.getToggleState();
        auto bc = btn.getBounds().toFloat();
        auto centre = bc.getCentre();
        float gap = 5.0f;
        float r = bc.getWidth() * 0.55f + gap;
        juce::Colour col = isOn ? onColour : offColour;
        juce::ColourGradient grad(col, centre.x, centre.y,
                                  juce::Colours::transparentBlack, centre.x + r, centre.y, true);
        g.setGradientFill(grad);
        g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
    };

    drawGlow(muteButton, juce::Colour(0x55ff3030), juce::Colour(0x22200808));
    drawGlow(soloButton, juce::Colour(0x55ffd030), juce::Colour(0x22281e04));
}

void UserChannelStrip::paint(juce::Graphics& g)
{
    auto dbFromPeak = [](float peak)
    {
        float p = juce::jlimit(1.0e-6f, 1.0f, peak);
        return 20.0f * std::log10(p);
    };
    auto colourForPeak = [&](float peak)
    {
        float db = dbFromPeak(peak);
        if (peak >= 0.999f) return juce::Colours::red;
        if (db > -3.0f)     return juce::Colours::orange;
        return juce::Colours::green;
    };

    g.fillAll(juce::Colours::black.withAlpha(0.45f));
    g.setColour(juce::Colours::black.withAlpha(0.60f));
    g.drawRect(getLocalBounds(), 1);

    const bool multiChan = isMultiChanPeer && numRemoteChannels > 1;
    // Only show wide per-channel meter bars when collapsed; when expanded the sub-faders show peaks
    const bool showMultiMeter = multiChan && !isExpanded;

    if (isHorizontalLayout)
    {
        auto sliderBounds = volumeSlider.getBounds();
        int meterWidth = 10; // Fixed width to prevent growing/shrinking
        juce::Rectangle<int> meterBounds(sliderBounds.getRight(), sliderBounds.getY(),
                                         meterWidth, sliderBounds.getHeight());

        g.setColour(juce::Colours::black);
        g.fillRect(meterBounds);

        if (showMultiMeter)
        {
            int n   = numRemoteChannels;
            int bw  = juce::jmax(1, meterBounds.getWidth() / n);
            int totalH = meterBounds.getHeight();
            for (int ch = 0; ch < n; ++ch)
            {
                float peak = channelPeaks[ch];
                int h = (int)(totalH * juce::jmin(peak, 1.0f));
                if (h > 0)
                {
                    juce::Rectangle<int> bar(meterBounds.getX() + ch * bw,
                                             meterBounds.getBottom() - h,
                                             bw - 1, h);
                    g.setColour(colourForPeak(peak));
                    g.fillRect(bar);
                }
            }
        }
        else
        {
            auto meterL = meterBounds.removeFromLeft(meterBounds.getWidth() / 2);
            auto meterR = meterBounds;

            int hL = (int)(meterL.getHeight() * juce::jmin(currentPeakL, 1.0f));
            int hR = (int)(meterR.getHeight() * juce::jmin(currentPeakR, 1.0f));

            if (hL > 0) { auto bar = meterL.removeFromBottom(hL); g.setColour(colourForPeak(currentPeakL)); g.fillRect(bar); }
            if (hR > 0) { auto bar = meterR.removeFromBottom(hR); g.setColour(colourForPeak(currentPeakR)); g.fillRect(bar); }
        }
    }
    else
    {
        auto sliderBounds = volumeSlider.getBounds();
        int meterHeight = 6; // Fixed height to prevent growing/shrinking
        juce::Rectangle<int> meterBounds(sliderBounds.getX(), sliderBounds.getBottom(),
                                         sliderBounds.getWidth(), meterHeight);

        g.setColour(juce::Colours::black);
        g.fillRect(meterBounds);

        if (showMultiMeter)
        {
            int n   = numRemoteChannels;
            int bh  = juce::jmax(1, meterBounds.getHeight() / n);
            int totalW = meterBounds.getWidth();
            for (int ch = 0; ch < n; ++ch)
            {
                float peak = channelPeaks[ch];
                int w = (int)(totalW * juce::jmin(peak, 1.0f));
                if (w > 0)
                {
                    juce::Rectangle<int> bar(meterBounds.getX(),
                                             meterBounds.getY() + ch * bh,
                                             w, bh - 1);
                    g.setColour(colourForPeak(peak));
                    g.fillRect(bar);
                }
            }
        }
        else
        {
            int w = meterBounds.getWidth();
            float maxP = juce::jmax(currentPeakL, currentPeakR);
            int wP = (int)(w * juce::jmin(maxP, 1.0f));

            if (wP > 0)
            {
                auto bar = meterBounds.removeFromLeft(wP);
                g.setColour(colourForPeak(maxP));
                g.fillRect(bar);
            }
        }
    }
}

void UserChannelStrip::resized()
{
    auto area = getLocalBounds().reduced(2);

    if (isHorizontalLayout)
    {
        // When multichan is expanded, restrict main strip to the left column
        if (isExpanded && isMultiChanPeer && numRemoteChannels > 1)
            area.setWidth(56); // 60px column minus 2px margin each side

        nameLabel.setBounds(area.removeFromTop(20));
        outputSelector.setBounds(area.removeFromBottom(20));
        auto dbArea   = area.removeFromBottom(16);
        auto ctrlArea = area.removeFromBottom(20);
        muteButton.setBounds(ctrlArea.removeFromLeft(area.getWidth() / 2));
        soloButton.setBounds(ctrlArea);
        panSlider.setBounds(area.removeFromTop(20).reduced(4, 2));

        int sliderWidth  = juce::jmin(20, area.getWidth());
        int sliderHeight = (int)(area.getHeight() * 0.85f);
        int sliderY      = area.getY() + (area.getHeight() - sliderHeight) / 2;
        volumeSlider.setBounds(area.getCentreX() - sliderWidth / 2, sliderY, sliderWidth, sliderHeight);
        volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
        dbLabel.setBounds(dbArea);
        dbLabel.setVisible(true);

        // Expand button at top-right of strip when multichannel peer
        if (isMultiChanPeer)
        {
            auto nameBounds = nameLabel.getBounds();
            expandButton.setBounds(nameBounds.getRight() - 14, nameBounds.getY(), 14, nameBounds.getHeight());
            expandButton.setVisible(true);
        }
        else
        {
            expandButton.setVisible(false);
        }

        // Per-channel faders as side columns to the right of main strip when expanded
        if (isExpanded && isMultiChanPeer && numRemoteChannels > 1)
        {
            auto subArea = getLocalBounds().reduced(2);
            subArea.removeFromLeft(60); // skip the narrower main column when expanded
            int colW = 36;
            for (int i = 0; i < numRemoteChannels; ++i)
            {
                auto col = subArea.removeFromLeft(colW);
                // Name label at top (18px), slider fills rest
                channelNameLabels[i].setBounds(col.removeFromTop(18).reduced(1, 0));
                channelNameLabels[i].setVisible(true);
                col.reduce(2, 4);
                channelSliders[i].setBounds(col);
                channelSliders[i].setSliderStyle(juce::Slider::LinearVertical);
                channelSliders[i].setVisible(true);
            }
            for (int i = numRemoteChannels; i < kMaxRemoteCh; ++i)
            {
                channelSliders[i].setVisible(false);
                channelNameLabels[i].setVisible(false);
            }
        }
        else
        {
            for (int i = 0; i < kMaxRemoteCh; ++i)
            {
                channelSliders[i].setVisible(false);
                channelNameLabels[i].setVisible(false);
            }
        }
    }
    else
    {
        // List layout (strip is horizontal)
        // When multichan is expanded, restrict main strip to the top row
        if (isExpanded && isMultiChanPeer && numRemoteChannels > 1)
            area.setHeight(36); // 40px base minus 2px padding top/bottom
        // Reserve 18px at right for expand button if this is a multichan peer
        if (isMultiChanPeer)
        {
            expandButton.setBounds(area.removeFromRight(18));
            expandButton.setVisible(true);
        }
        else
        {
            expandButton.setVisible(false);
        }

        nameLabel.setBounds(area.removeFromLeft(80));
        outputSelector.setBounds(area.removeFromRight(60));
        auto ctrlArea = area.removeFromRight(40);
        muteButton.setBounds(ctrlArea.removeFromTop(ctrlArea.getHeight() / 2));
        soloButton.setBounds(ctrlArea);
        panSlider.setBounds(area.removeFromRight(40));

        // Leave a fixed room for the meter rows at the bottom of the main strip
        int meterH = 6;
        area.removeFromBottom(meterH);
        int sliderHeight = juce::jmin(18, area.getHeight());
        volumeSlider.setBounds(area.getX(), area.getCentreY() - sliderHeight / 2, area.getWidth(), sliderHeight);
        volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        dbLabel.setVisible(false);

        // Per-channel rows below the main strip when expanded
        if (isExpanded && isMultiChanPeer && numRemoteChannels > 1)
        {
            auto expandArea = getLocalBounds().reduced(2);
            expandArea.removeFromTop(40); // skip the main strip row
            int rowH = 36;
            for (int i = 0; i < numRemoteChannels; ++i)
            {
                auto row = expandArea.removeFromTop(rowH);
                row.removeFromLeft(6);
                // Left 80px: channel name; rest: slider
                channelNameLabels[i].setBounds(row.removeFromLeft(80));
                channelNameLabels[i].setVisible(true);
                channelSliders[i].setBounds(row.reduced(0, 2));
                channelSliders[i].setSliderStyle(juce::Slider::LinearHorizontal);
                channelSliders[i].setVisible(true);
            }
            for (int i = numRemoteChannels; i < kMaxRemoteCh; ++i)
            {
                channelSliders[i].setVisible(false);
                channelNameLabels[i].setVisible(false);
            }
        }
        else
        {
            for (int i = 0; i < kMaxRemoteCh; ++i)
            {
                channelSliders[i].setVisible(false);
                channelNameLabels[i].setVisible(false);
            }
        }
    }
}

int UserChannelStrip::getPreferredHeight() const
{
    int base = 40;
    if (!isHorizontalLayout && isExpanded && isMultiChanPeer && numRemoteChannels > 1)
        return base + numRemoteChannels * 36;
    return base;
}

int UserChannelStrip::getPreferredWidth() const
{
    if (isHorizontalLayout && isExpanded && isMultiChanPeer && numRemoteChannels > 1)
        return 60 + numRemoteChannels * 36; // narrower main + wider sub-channels
    return 80;
}

void UserChannelStrip::setOrientation(bool isHorizontal)
{
    isHorizontalLayout = isHorizontal;

    if (isHorizontalLayout)
    {
        panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        panSlider.setLookAndFeel(&panLookAndFeel);
    }
    else
    {
        panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        panSlider.setLookAndFeel(nullptr);
    }

    resized();
    repaint();
}

void UserChannelStrip::updateInfo(const NinjamVst3AudioProcessor::UserInfo& info)
{
    userIndex = info.index;
    userInfo  = info;
    nameLabel.setText(info.name, juce::dontSendNotification);

    if (!volumeSlider.isMouseOverOrDragging())
        volumeSlider.setValue(juce::jmin(info.volume, 2.0f), juce::dontSendNotification);

    if (!panSlider.isMouseOverOrDragging())
        panSlider.setValue(info.pan, juce::dontSendNotification);

    muteButton.setToggleState(info.isMuted, juce::dontSendNotification);

    // Sync multichan state — trigger layout refresh if anything changed
    const int newNCh = juce::jlimit(1, kMaxRemoteCh, info.numChannels);
    const bool multiStateChanged = (info.isMultiChanPeer != isMultiChanPeer) || (newNCh != numRemoteChannels);
    isMultiChanPeer   = info.isMultiChanPeer;
    numRemoteChannels = newNCh;

    // Update channel name labels
    for (int i = 0; i < kMaxRemoteCh; ++i)
    {
        juce::String name = i < info.channelNames.size() ? info.channelNames[i] : "";
        channelNameLabels[i].setText(name, juce::dontSendNotification);
    }
    if (multiStateChanged)
    {
        // Set button text to match current state
        if (isMultiChanPeer)
            expandButton.setButtonText(isHorizontalLayout ? ">" : "v");
        // If we lost multichan, collapse
        if (!isMultiChanPeer && isExpanded)
        {
            isExpanded = false;
            expandButton.setButtonText(isHorizontalLayout ? ">" : "v");
        }
        resized();
        repaint();
        // Walk up to UserListComponent so it recalculates strip heights
        for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
        {
            if (auto* list = dynamic_cast<UserListComponent*>(p))
            {
                list->resized();
                break;
            }
        }
    }

    int totalOutputs = processor.getTotalNumOutputChannels();
    if (totalOutputs <= 0) totalOutputs = 2;
    int numPairs    = totalOutputs / 2;
    int stereoBaseId = 100;

    int ch = info.outputChannel;
    int id = 0;
    bool isMono = (ch & 1024) != 0;
    int chanIdx  = ch & 1023;
    if (isMono)
    {
        if (chanIdx >= 0 && chanIdx < totalOutputs)
            id = chanIdx + 1;
    }
    else
    {
        int pair = chanIdx / 2;
        if (pair >= 0 && pair < numPairs)
            id = stereoBaseId + pair;
    }

    if (id > 0 && outputSelector.getSelectedId() != id)
        outputSelector.setSelectedId(id, juce::dontSendNotification);
}

void UserChannelStrip::setClipEnabled(bool enabled)
{
    clipButton.setToggleState(enabled, juce::dontSendNotification);
    processor.setUserClipEnabled(userIndex, enabled);
}

void UserChannelStrip::timerCallback()
{
    for (auto* c = getParentComponent(); c != nullptr; c = c->getParentComponent())
        if (auto* editor = dynamic_cast<NinjamVst3AudioProcessorEditor*>(c))
            if (editor->shouldDeferHeavyUiWork())
                return;

    auto peakL = processor.getUserPeak(userIndex, 0);
    auto peakR = processor.getUserPeak(userIndex, 1);

    bool needRepaint = false;

    if (std::abs(peakL - currentPeakL) > 0.001f || std::abs(peakR - currentPeakR) > 0.001f)
    {
        currentPeakL = peakL;
        currentPeakR = peakR;
        float peak = juce::jmax(currentPeakL, currentPeakR);
        float db = -60.0f;
        if (peak > 1.0e-6f)
            db = juce::jlimit(-60.0f, 6.0f, 20.0f * std::log10(peak));
        dbLabel.setText(juce::String(db, 1) + " dB", juce::dontSendNotification);
        needRepaint = true;
    }

    // Update per-NINJAM-channel peaks for multichan peers (used by collapsed multi-meter + expanded rows)
    if (isMultiChanPeer && numRemoteChannels > 1)
    {
        for (int ch = 0; ch < numRemoteChannels; ++ch)
        {
            // NINJAM ch0 = Vorbis mixdown; individual channels start at ch1
            float chPeak = processor.getUserChannelPeak(userIndex, ch + 1, -1); // -1 = both/max
            if (std::abs(chPeak - channelPeaks[ch]) > 0.001f)
            {
                channelPeaks[ch] = chPeak;
                needRepaint = true;
            }
        }
    }

    if (needRepaint)
        repaint();
}

void UserChannelStrip::volumeChanged()
{
    applyVolumesToProcessor();
}

void UserChannelStrip::panChanged()
{
    applyVolumesToProcessor();
}

void UserChannelStrip::outputChanged()
{
    int selectedId = outputSelector.getSelectedId();
    if (selectedId <= 0)
        return;

    int totalOutputs = processor.getTotalNumOutputChannels();
    if (totalOutputs <= 0) totalOutputs = 2;
    int numPairs    = totalOutputs / 2;
    int stereoBaseId = 100;

    if (selectedId >= 1 && selectedId <= totalOutputs)
        // Single (mono) channel: set the 1024 mono bit so njclient outputs to one channel only
        processor.setUserOutput(userIndex, (selectedId - 1) | 1024);
    else if (selectedId >= stereoBaseId && selectedId < stereoBaseId + numPairs)
        // Stereo pair: no mono bit, base channel is pair * 2
        processor.setUserOutput(userIndex, (selectedId - stereoBaseId) * 2);
}

void UserChannelStrip::muteChanged()
{
    applyVolumesToProcessor();
}

void UserChannelStrip::soloChanged()
{
    applyVolumesToProcessor();
}

void UserChannelStrip::clipChanged()
{
    processor.setUserClipEnabled(userIndex, clipButton.getToggleState());
}

void UserChannelStrip::toggleExpanded()
{
    isExpanded = !isExpanded;
    if (isHorizontalLayout)
        expandButton.setButtonText(isExpanded ? "<" : ">");
    else
        expandButton.setButtonText(isExpanded ? "^" : "v");
    resized();
    // Walk up to UserListComponent to trigger full height recalculation
    for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
    {
        if (auto* list = dynamic_cast<UserListComponent*>(p))
        {
            list->resized();
            break;
        }
    }
}

void UserChannelStrip::applyVolumesToProcessor()
{
    float mv   = (float)volumeSlider.getValue();
    float pan  = (float)panSlider.getValue();
    bool mute  = muteButton.getToggleState();
    bool solo  = soloButton.getToggleState();
    processor.setUserLevel(userIndex, mv, pan, mute, solo);
    // Re-apply per-channel gain overrides for multichan peers
    if (isMultiChanPeer && numRemoteChannels > 1)
    {
        for (int ch = 0; ch < numRemoteChannels; ++ch)
            // NINJAM ch0 = Vorbis mixdown; individual channels start at ch1
            processor.setUserNjChannelVolume(userIndex, ch + 1, mv * perChannelGain[ch]);
    }
}

// ==============================================================================
// UserListComponent Implementation
// ==============================================================================

UserListComponent::UserListComponent(NinjamVst3AudioProcessor& p)
    : processor(p)
{
    setOpaque(false);
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentComponent, false);
    viewport.setScrollBarsShown(true, true);
    contentComponent.setOpaque(false);
}

UserListComponent::~UserListComponent()
{
    strips.clear();
}

void UserListComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.30f));
}

void UserListComponent::resized()
{
    viewport.setBounds(getLocalBounds());

    int stripWidth  = isHorizontal ? 80 : viewport.getWidth() - 15;
    int defHeight   = isHorizontal ? viewport.getHeight() - 20 : 40;
    if (stripWidth < 10) stripWidth = 10;
    if (defHeight  < 10) defHeight  = 10;

    int x = 0, y = 0;
    for (auto& strip : strips)
    {
        int sw = isHorizontal ? strip->getPreferredWidth() : stripWidth;
        int sh = isHorizontal ? defHeight : strip->getPreferredHeight();
        strip->setBounds(x, y, sw, sh);
        if (isHorizontal) x += sw;
        else              y += sh;
    }

    if (isHorizontal)
        contentComponent.setBounds(0, 0, x, viewport.getHeight() - 20);
    else
        contentComponent.setBounds(0, 0, viewport.getWidth() - 15, juce::jmax(y, viewport.getHeight() - 20));
}

void UserListComponent::updateContent()
{
    auto users = processor.getConnectedUsers();

    if (users.size() != strips.size())
    {
        strips.clear();
        contentComponent.removeAllChildren();

        for (const auto& u : users)
        {
            auto strip = std::make_unique<UserChannelStrip>(processor, u.index);
            strip->setOrientation(isHorizontal);
            strip->updateInfo(u);
            strip->setClipEnabled(processor.isSoftLimiterEnabled());
            contentComponent.addAndMakeVisible(strip.get());
            strips.push_back(std::move(strip));
        }
        resized();
    }
    else
    {
        for (size_t i = 0; i < users.size(); ++i)
            strips[i]->updateInfo(users[i]);
    }
}

void UserListComponent::setLayoutMode(bool horizontal)
{
    isHorizontal = horizontal;
    for (auto& strip : strips)
        strip->setOrientation(horizontal);
    resized();
}

void UserListComponent::setAllClipEnabled(bool enabled)
{
    for (auto& strip : strips)
        strip->setClipEnabled(enabled);
}

std::vector<UserChannelStrip*> UserListComponent::getStripPointers() const
{
    std::vector<UserChannelStrip*> pointers;
    pointers.reserve(strips.size());
    for (const auto& strip : strips)
        pointers.push_back(strip.get());
    return pointers;
}
#if 0
void FaderLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle style, juce::Slider& slider) { if (auto* editor = dynamic_cast<NinjamVst3AudioProcessorEditor*>(slider.getParentComponent())) { if (editor->faderKnobImage.isValid()) { auto isVertical = style == juce::Slider::LinearVertical; auto thumbWidth = isVertical ? width * 0.8f : 30.0f; auto thumbHeight = isVertical ? 30.0f : height * 0.8f; auto thumbX = isVertical ? (float)x + (float)width * 0.1f : sliderPos - thumbWidth * 0.5f; auto thumbY = isVertical ? sliderPos - thumbHeight * 0.5f : (float)y + (float)height * 0.1f; g.drawImageWithin(editor->faderKnobImage, (int)thumbX, (int)thumbY, (int)thumbWidth, (int)thumbHeight, juce::RectanglePlacement::centred); return; } } auto thumbWidth = (style == juce::Slider::LinearVertical) ? width * 0.8f : 12.0f; auto thumbHeight = (style == juce::Slider::LinearVertical) ? 12.0f : height * 0.8f; auto thumbX = (style == juce::Slider::LinearVertical) ? (float)x + (float)width * 0.1f : sliderPos - thumbWidth * 0.5f; auto thumbY = (style == juce::Slider::LinearVertical) ? sliderPos - thumbHeight * 0.5f : (float)y + (float)height * 0.1f; g.setColour(juce::Colour(0xff666666)); g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 2.0f); }
void CustomKnobLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) { auto centreX = (float)x + (float)width * 0.5f; auto centreY = (float)y + (float)height * 0.5f; auto* editor = dynamic_cast<NinjamVst3AudioProcessorEditor*>(slider.getParentComponent()); if (editor == nullptr) { auto* p = slider.getParentComponent(); while (p != nullptr && editor == nullptr) { editor = dynamic_cast<NinjamVst3AudioProcessorEditor*>(p); p = p->getParentComponent(); } } if (editor != nullptr && editor->radioKnobImage.isValid()) { const float radius = (float)juce::jmin(width / 2, height / 2) - 4.0f; auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle); g.drawImageWithin(editor->radioKnobImage, (int)(centreX - radius), (int)(centreY - radius), (int)(radius * 2.0f), (int)(radius * 2.0f), juce::RectanglePlacement::fillDestination); return; } auto radius = (float)juce::jmin(width / 2, height / 2) - 10.0f; g.setColour(juce::Colour(0xffdddddd)); g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f); }
NinjamVst3AudioProcessorEditor::NinjamVst3AudioProcessorEditor (NinjamVst3AudioProcessor& p) : AudioProcessorEditor (&p), audioProcessor (p), intervalDisplay(p), userList(p) { setSize (1120, 620); setResizable(true, true); addAndMakeVisible(serverField); addAndMakeVisible(serverListButton); addAndMakeVisible(backgroundSelector); backgroundSelector.onChange = [this] { auto files = juce::File(\ C:\\\Users\\\mcand\\\Pictures\\\textures\).findChildFiles(juce::File::findFiles, false, \*.jpg\); int idx = backgroundSelector.getSelectedItemIndex(); if (idx >= 0 ; idx < files.size()) { backgroundImage = juce::ImageFileFormat::loadFrom(files[idx]); loadControlImages(files[idx]); repaint(); } }; loadControlImages(juce::File(\C:\\\Users\\\mcand\\\Pictures\\\textures\\\Brushed Metal 1.jpg\)); } NinjamVst3AudioProcessorEditor::~NinjamVst3AudioProcessorEditor() {} void NinjamVst3AudioProcessorEditor::paint (juce::Graphics& g) { if (backgroundImage.isValid()) g.drawImageWithin(backgroundImage, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::fillDestination); else g.fillAll(juce::Colour(0xff222222)); } void NinjamVst3AudioProcessorEditor::resized() { auto header = getLocalBounds().removeFromTop(40); backgroundSelector.setBounds(header.removeFromRight(150).reduced(2)); } void NinjamVst3AudioProcessorEditor::loadControlImages(const juce::File& f) { auto dir = f.getParentDirectory(); auto base = f.getFileNameWithoutExtension(); radioKnobImage = juce::ImageFileFormat::loadFrom(dir.getChildFile(base + \_radioknob.png\)); faderKnobImage = juce::ImageFileFormat::loadFrom(dir.getChildFile(base + \_faderknob.png\)); repaint(); }
#endif
