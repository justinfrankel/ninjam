#include "ProVideoCodec.h"

//==============================================================================
// Internal helpers
//==============================================================================
namespace
{
    //--------------------------------------------------------------------------
    // Hardware encoder try-list (CPU libx264 always last)
    //--------------------------------------------------------------------------
    static const char* const kH264EncoderNames[] = {
#if defined(_WIN32)
        "h264_nvenc",
        "h264_amf",
        "h264_qsv",
#elif defined(__APPLE__)
        "h264_videotoolbox",
        "h264_nvenc",
#else   // Linux
        "h264_nvenc",
        "h264_vaapi",
#endif
        "libx264",
        nullptr
    };

} // namespace

//==============================================================================
// ProVideoEncoder
//==============================================================================
//==============================================================================
// Annex-B NALU helpers (used by the encoder to cache and re-inject SPS+PPS)

/// Returns the byte offset of the first IDR-slice start code (00 00 00 01 65)
/// within an Annex-B byte stream, or -1 if not found.
static int findFirstIdrOffset(const uint8_t* data, int len)
{
    for (int i = 0; i + 4 < len; ++i)
    {
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1
                && (data[i+4] & 0x1f) == 5)
            return i;
    }
    return -1;
}

/// Returns true if the Annex-B stream begins with an SPS NALU (type 7).
static bool startsWithSps(const uint8_t* data, int len)
{
    if (len >= 5 && data[0]==0 && data[1]==0 && data[2]==0 && data[3]==1 && (data[4]&0x1f)==7)
        return true;
    if (len >= 4 && data[0]==0 && data[1]==0 && data[2]==1 && (data[3]&0x1f)==7)
        return true;
    return false;
}

//==============================================================================
bool ProVideoEncoder::tryOpenEncoder(const char* codecName, int width, int height, int fps, int bitrate)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(codecName);
    if (codec == nullptr)
        return false;

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (ctx == nullptr)
        return false;

    ctx->width        = width;
    ctx->height       = height;
    ctx->pix_fmt      = AV_PIX_FMT_YUV420P;
    ctx->time_base    = { 1, fps };
    ctx->framerate    = { fps, 1 };
    ctx->bit_rate     = bitrate;
    ctx->gop_size     = 1;   // Every frame is an IDR keyframe — each NINJAM interval is independent.
    ctx->max_b_frames = 0;

    // Tune — errors from av_opt_set are non-fatal
    av_opt_set(ctx->priv_data, "preset", "ultrafast", AV_OPT_SEARCH_CHILDREN);
    av_opt_set(ctx->priv_data, "tune",   "zerolatency", AV_OPT_SEARCH_CHILDREN);
    // Embed SPS+PPS in every IDR frame so late-joining decoders can start anywhere.
    av_opt_set_int(ctx->priv_data, "repeat-headers", 1, 0);           // nvenc/amf/qsv
    av_opt_set(ctx->priv_data, "x264-params", "repeat-headers=1", 0); // libx264

    if (avcodec_open2(ctx, codec, nullptr) < 0)
    {
        avcodec_free_context(&ctx);
        return false;
    }

    AVFrame* f = av_frame_alloc();
    if (f == nullptr)
    {
        avcodec_free_context(&ctx);
        return false;
    }
    f->format = ctx->pix_fmt;
    f->width  = width;
    f->height = height;
    if (av_frame_get_buffer(f, 32) < 0)
    {
        av_frame_free(&f);
        avcodec_free_context(&ctx);
        return false;
    }

    SwsContext* sws = sws_getContext(
        width, height, AV_PIX_FMT_RGB24,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (sws == nullptr)
    {
        av_frame_free(&f);
        avcodec_free_context(&ctx);
        return false;
    }

    codecCtx   = ctx;
    frame      = f;
    swsCtx     = sws;
    openWidth  = width;
    openHeight = height;
    pts        = 0;
    DBG("ProVideoEncoder: opened " << juce::String(codecName) << " " << width << "x" << height);
    return true;
}

bool ProVideoEncoder::open(int width, int height, int fps, int bitrate)
{
    if (isOpen() && openWidth == width && openHeight == height)
        return true;

    close();

    for (int i = 0; kH264EncoderNames[i] != nullptr; ++i)
    {
        if (tryOpenEncoder(kH264EncoderNames[i], width, height, fps, bitrate))
            return true;
    }
    DBG("ProVideoEncoder: all encoders failed for " << width << "x" << height);
    return false;
}

bool ProVideoEncoder::encodeFrame(const juce::Image& img, juce::MemoryBlock& outData)
{
    if (codecCtx == nullptr || frame == nullptr || swsCtx == nullptr)
        return false;

    const juce::Image rgb = img.convertedToFormat(juce::Image::RGB);
    juce::Image::BitmapData bd(rgb, juce::Image::BitmapData::readOnly);

    const int         srcStride[1] = { bd.lineStride };
    const uint8_t*    src[1]       = { bd.getLinePointer(0) };

    if (av_frame_make_writable(frame) < 0)
        return false;

    sws_scale(swsCtx, src, srcStride, 0, img.getHeight(),
              frame->data, frame->linesize);

    frame->pts = pts++;

    if (avcodec_send_frame(codecCtx, frame) < 0)
        return false;

    bool gotData = false;
    for (;;)
    {
        AVPacket* pkt = av_packet_alloc();
        if (pkt == nullptr)
            break;

        const int ret = avcodec_receive_packet(codecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_packet_free(&pkt);
            break;
        }
        if (ret < 0)
        {
            av_packet_free(&pkt);
            return false;
        }

        // Cache the SPS+PPS block from the first packet (everything before the
        // IDR slice).  Prepend it to every subsequent IDR-only packet so that
        // any decoder — including one that joins after the first packet was sent
        // — can always decode the frame independently.
        const uint8_t* pktData = pkt->data;
        const int      pktSize = pkt->size;
        if (cachedSpsAndPps.isEmpty())
        {
            const int idrOff = findFirstIdrOffset(pktData, pktSize);
            if (idrOff > 0)
                cachedSpsAndPps.append(pktData, (size_t) idrOff);
        }
        if (!cachedSpsAndPps.isEmpty() && !startsWithSps(pktData, pktSize))
            outData.append(cachedSpsAndPps.getData(), cachedSpsAndPps.getSize());
        outData.append(pktData, static_cast<size_t>(pktSize));
        gotData = true;
        av_packet_free(&pkt);
    }

    return gotData;
}

void ProVideoEncoder::closeInternal()
{
    cachedSpsAndPps.reset(); // clear cached headers so the next open starts fresh
    if (swsCtx)   { sws_freeContext(swsCtx);        swsCtx   = nullptr; }
    if (frame)    { av_frame_free(&frame);           frame    = nullptr; }
    if (codecCtx) { avcodec_free_context(&codecCtx); codecCtx = nullptr; }
    openWidth  = 0;
    openHeight = 0;
    pts        = 0;
}

void ProVideoEncoder::close()
{
    closeInternal();
}

//==============================================================================
// ProVideoDecoder — stateful per-user H.264 decoder
//
// The encoder produces raw Annex-B H.264.  With gop_size=1 the first packet
// contains SPS+PPS+IDR; subsequent packets contain IDR only (libx264 only
// emits SPS/PPS once unless repeat-headers is set).  A fresh AVCodecContext
// per call would lose SPS/PPS context for packets 1+, so we keep the context
// alive for the lifetime of the decoder instance.
//
// We skip the AVCodecParser entirely: each packet we receive is already a
// complete Annex-B bitstream assembled by the encoder loop, so we pass it
// directly to avcodec_send_packet.
//==============================================================================

bool ProVideoDecoder::ensureOpen()
{
    if (codecCtx != nullptr)
        return true;

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr)
    {
        DBG("ProVideoDecoder: H.264 decoder not found");
        return false;
    }

    codecCtx = avcodec_alloc_context3(codec);
    if (codecCtx == nullptr)
        return false;

    // Minimise latency — we don't use B-frames.
    codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    if (avcodec_open2(codecCtx, codec, nullptr) < 0)
    {
        avcodec_free_context(&codecCtx);
        DBG("ProVideoDecoder: avcodec_open2 failed");
        return false;
    }

    avFrame = av_frame_alloc();
    if (avFrame == nullptr)
    {
        avcodec_free_context(&codecCtx);
        return false;
    }

    return true;
}

void ProVideoDecoder::close()
{
    if (swsCtx != nullptr)
    {
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
        swsW   = 0;
        swsH   = 0;
        swsFmt = AV_PIX_FMT_NONE;
    }
    if (avFrame  != nullptr) { av_frame_free(&avFrame);         avFrame  = nullptr; }
    if (codecCtx != nullptr) { avcodec_free_context(&codecCtx); codecCtx = nullptr; }
}

bool ProVideoDecoder::decode(const void* data, int dataLen, juce::Image& outImage)
{
    if (data == nullptr || dataLen <= 0)
        return false;

    if (!ensureOpen())
        return false;

    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr)
        return false;

    // Pass the full Annex-B bitstream as one packet.  The H.264 decoder
    // handles embedded start codes (00 00 00 01 ...) natively.
    pkt->data  = const_cast<uint8_t*>(static_cast<const uint8_t*>(data));
    pkt->size  = dataLen;
    pkt->flags = AV_PKT_FLAG_KEY;

    juce::Image result;

    if (avcodec_send_packet(codecCtx, pkt) == 0)
    {
        const int ret = avcodec_receive_frame(codecCtx, avFrame);
        if (ret == 0)
        {
            const int w = avFrame->width;
            const int h = avFrame->height;
            const auto fmt = static_cast<AVPixelFormat>(avFrame->format);

            // Reuse the sws context if the frame geometry hasn't changed.
            if (swsCtx == nullptr || swsW != w || swsH != h || swsFmt != fmt)
            {
                if (swsCtx != nullptr)
                    sws_freeContext(swsCtx);
                swsCtx = sws_getContext(w, h, fmt,
                                        w, h, AV_PIX_FMT_RGB24,
                                        SWS_BICUBIC, nullptr, nullptr, nullptr);
                swsW   = w;
                swsH   = h;
                swsFmt = fmt;
            }

            if (swsCtx != nullptr)
            {
                juce::Image img(juce::Image::RGB, w, h, false);
                {
                    juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
                    uint8_t*  dstSlice[1]  = { bd.getLinePointer(0) };
                    const int dstStride[1] = { bd.lineStride };
                    sws_scale(swsCtx, avFrame->data, avFrame->linesize,
                              0, h, dstSlice, dstStride);
                }
                result = img;
            }
            av_frame_unref(avFrame);
        }
    }

    // Null out data so av_packet_free doesn't try to unref a non-owned buffer.
    pkt->data = nullptr;
    pkt->size = 0;
    av_packet_free(&pkt);

    if (!result.isValid())
    {
        DBG("ProVideoDecoder: decode failed for " << dataLen << " bytes");
        return false;
    }

    outImage = std::move(result);
    return true;
}
