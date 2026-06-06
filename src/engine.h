#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

class Decoder;
class Renderer;
class Monitor;

class Engine {
public:
    Engine();
    ~Engine();

    void play(const std::string& path);
    void stop();
    void resume();
    void quit();
    void setSpeed(double speed);

    bool isActive() const { return m_active.load(); }
    bool isPlaying() const { return m_playing.load(); }
    bool hasError() const { return !m_error.empty(); }
    std::string error() const;
    std::string status() const;
    std::string videoInfo() const;

private:
    void threadFunc();
    void updateStatus(const std::string& s);

    std::thread m_thread;
    std::atomic<bool> m_quit{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_stopRequested{false};

    mutable std::mutex m_mutex;
    std::string m_path;
    std::string m_status;
    std::string m_error;
    std::string m_videoInfo;
    std::atomic<bool> m_pathChanged{false};
    std::atomic<double> m_speed{1.0};
};
