#include "async_logger.h"

// Define the global instance
AsyncLogger global_logger;

AsyncLogger::AsyncLogger() : is_running(true) {
    // Launch the background worker thread immediately upon creation
    worker_thread = std::thread(&AsyncLogger::process_logs, this);
}

AsyncLogger::~AsyncLogger() {
    is_running = false;
    
    // Wake up the worker thread one last time so it can exit its loop
    cv.notify_one(); 
    
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void AsyncLogger::log(const std::string& log_message) {
    {
        // Lock the queue, push the string, and instantly unlock
        std::lock_guard<std::mutex> lock(queue_mutex);
        log_queue.push(log_message);
    }
    // Ping the background thread to wake up and print the new message
    cv.notify_one();
}

void AsyncLogger::process_logs() {
    while (is_running) {
        std::string pending_message;

        {
            // Lock the queue to check for work
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            // Go to sleep until the queue has data OR the program is shutting down
            cv.wait(lock, [this]() { 
                return !log_queue.empty() || !is_running; 
            });

            // If we woke up because of shutdown and the queue is empty, exit
            if (!is_running && log_queue.empty()) {
                break;
            }

            // Pop the message off the queue
            pending_message = log_queue.front();
            log_queue.pop();
        } // Mutex is automatically unlocked here!

        // Print to the slow terminal screen WITHOUT holding the lock
        std::cout << "[LOG] " << pending_message << std::endl;
    }
}