#include "decoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

Decoder::Decoder(const std::string& path) : m_path(path) {
    openCodec();
}

Decoder::~Decoder() {
    stop();
    closeCodec();
}

bool Decoder::openCodec() {
    if (avformat_open_input(&m_fmtCtx, m_path.c_str(), nullptr, nullptr) < 0)
        return false;

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0)
        return false;

    for (unsigned i = 0; i < m_fmtCtx->nb_streams; i++) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIdx = i;
            break;
        }
    }
    if (m_videoStreamIdx < 0) return false;

    auto* codecpar = m_fmtCtx->streams[m_videoStreamIdx]->codecpar;
    auto* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) return false;

    m_codecCtx = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(m_codecCtx, codecpar) < 0)
        return false;
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0)
        return false;

    m_width = codecpar->width;
    m_height = codecpar->height;

    auto* stream = m_fmtCtx->streams[m_videoStreamIdx];
    if (stream->r_frame_rate.den > 0)
        m_fps = av_q2d(stream->r_frame_rate);

    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();

    m_swsCtx = sws_getContext(
        m_width, m_height, static_cast<AVPixelFormat>(codecpar->format),
        m_width, m_height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    if (!m_swsCtx) return false;

    m_valid = true;
    return true;
}

void Decoder::closeCodec() {
    av_frame_free(&m_frame);
    av_packet_free(&m_packet);
    sws_freeContext(m_swsCtx);
    m_swsCtx = nullptr;
    avcodec_free_context(&m_codecCtx);
    avformat_close_input(&m_fmtCtx);
}

void Decoder::start() {
    if (!m_valid || m_running.load()) return;
    m_running = true;
    m_seekEof = false;
    m_thread = std::thread(&Decoder::decodeLoop, this);
}

void Decoder::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void Decoder::setTargetFps(double fps) {
    m_targetFps.store(fps);
}

std::shared_ptr<Frame> Decoder::read() {
    return m_latestFrame.exchange(nullptr);
}

void Decoder::decodeLoop() {
    auto lastFrameTime = std::chrono::steady_clock::now();

    while (m_running.load()) {
        double targetFps = m_targetFps.load();
        if (targetFps <= 0.0) targetFps = m_fps;
        targetFps *= m_speed.load();
        if (targetFps < 0.5) targetFps = 0.5;

        auto interval = std::chrono::microseconds(static_cast<int64_t>(1000000.0 / targetFps));
        auto elapsed = std::chrono::steady_clock::now() - lastFrameTime;
        if (elapsed < interval) {
            std::this_thread::sleep_for(interval - elapsed);
        }

        int ret = av_read_frame(m_fmtCtx, m_packet);
        if (ret < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            av_seek_frame(m_fmtCtx, m_videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(m_codecCtx);
            continue;
        }

        if (m_packet->stream_index != m_videoStreamIdx) {
            av_packet_unref(m_packet);
            continue;
        }

        ret = avcodec_send_packet(m_codecCtx, m_packet);
        av_packet_unref(m_packet);
        if (ret < 0) continue;

        ret = avcodec_receive_frame(m_codecCtx, m_frame);
        if (ret < 0) continue;

        auto frame = std::make_shared<Frame>();
        frame->width = m_width;
        frame->height = m_height;

        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, m_width, m_height, 1);
        frame->data.resize(numBytes);

        uint8_t* dst[4] = { frame->data.data(), nullptr, nullptr, nullptr };
        int dstStride[4] = { m_width * 3, 0, 0, 0 };

        if (m_swsCtx)
            sws_scale(m_swsCtx, m_frame->data, m_frame->linesize,
                      0, m_height, dst, dstStride);

        m_latestFrame.store(frame);
        lastFrameTime = std::chrono::steady_clock::now();
    }
}
