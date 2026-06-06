#pragma once
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "frame.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct AVFrame;
struct AVPacket;

class Decoder {
public:
    explicit Decoder(const std::string& path);
    ~Decoder();

    bool valid() const { return m_valid; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double fps() const { return m_fps; }

    void start();
    void stop();
    void setTargetFps(double fps);
    void setSpeed(double speed) { m_speed.store(speed); }

    std::shared_ptr<Frame> read();

private:
    void decodeLoop();
    bool openCodec();
    void closeCodec();

    std::string m_path;
    bool m_valid = false;
    int m_width = 0, m_height = 0;
    double m_fps = 30.0;

    AVFormatContext* m_fmtCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    int m_videoStreamIdx = -1;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_seekEof{false};
    std::atomic<double> m_targetFps{0.0};
    std::atomic<double> m_speed{1.0};

    std::atomic<std::shared_ptr<Frame>> m_latestFrame{nullptr};
};
