#pragma once
#include <iostream>
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class AsyncLogger {
private:
    std::queue<std::string> log_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::thread worker_thread;
    std::atomic<bool> is_running;

    // The infinite loop run by the background thread
    void process_logs();

public:
    AsyncLogger();
    ~AsyncLogger();

    // Prevent copying
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    // The function your network threads will call
    void log(const std::string& message);
};

// Global instance declaration for your tracker
extern AsyncLogger global_logger;