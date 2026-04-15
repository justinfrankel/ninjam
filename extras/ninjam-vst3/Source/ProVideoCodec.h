#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

//==============================================================================
/** Encodes juce::Image frames to H.264 NAL byte-streams using FFmpeg.
    Call open() once before the first encodeFrame(), close() when done.
    Every frame is emitted as an IDR keyframe (gop_size=1) so that each
    NINJAM interval fragment is independently decodeable. */
class ProVideoEncoder
{
public:
    ProVideoEncoder() = default;
    ~ProVideoEncoder() { close(); }

    /** Open (or re-open) the encoder for the given dimensions and frame rate.
        Hardware encoders are tried in order: h264_nvenc → h264_amf → h264_qsv →
        h264_videotoolbox → libx264.  Returns true if a suitable encoder was found. */
    bool open(int width, int height, int fps, int bitrate);

    /** Encode one frame.  On success returns true and appends the raw H.264
        NAL bytes (without any container) to @p outData. */
    bool encodeFrame(const juce::Image& img, juce::MemoryBlock& outData);

    /** Release all FFmpeg resources. Safe to call multiple times. */
    void close();

    bool isOpen() const noexcept { return codecCtx != nullptr; }
    int getWidth()  const noexcept { return openWidth; }
    int getHeight() const noexcept { return openHeight; }

private:
    AVCodecContext* codecCtx = nullptr;
    AVFrame*        frame    = nullptr;
    SwsContext*     swsCtx   = nullptr;
    int64_t         pts      = 0;
    int             openWidth  = 0;
    int             openHeight = 0;

    bool tryOpenEncoder(const char* codecName, int width, int height, int fps, int bitrate);
    void closeInternal();

    // SPS+PPS cached from the first encoded packet and prepended to every
    // subsequent IDR-only packet so late-joining decoders can start anywhere.
    juce::MemoryBlock cachedSpsAndPps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProVideoEncoder)
};

//==============================================================================
/** Decodes H.264 NAL byte-streams to juce::Image frames using FFmpeg.
    Stateful — the codec context is kept open across calls so that SPS/PPS
    information from earlier packets (frame 0) is available when decoding
    subsequent IDR-only packets (frames 1+).  Create one instance per remote
    user and reuse it for the lifetime of the session. */
class ProVideoDecoder
{
public:
    ProVideoDecoder() = default;
    ~ProVideoDecoder() { close(); }

    /** Decode @p dataLen bytes of raw Annex-B H.264 data into @p outImage (RGB).
        Returns true on success.  Lazily opens the codec context on the first call. */
    bool decode(const void* data, int dataLen, juce::Image& outImage);

    /** Release all FFmpeg resources.  Safe to call multiple times. */
    void close();

private:
    AVCodecContext* codecCtx = nullptr;
    AVFrame*        avFrame  = nullptr;
    SwsContext*     swsCtx   = nullptr;
    int             swsW     = 0;
    int             swsH     = 0;
    AVPixelFormat   swsFmt   = AV_PIX_FMT_NONE;

    bool ensureOpen();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProVideoDecoder)
};
