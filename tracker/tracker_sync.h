#pragma once

#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include "user_manager.h"
#include "group_manager.h"
#include "file_manager.h"
#include "../common/network_utils.h"

class TrackerSync {
private:
    std::string peer_ip;
    int peer_port;
    std::atomic<int> peer_sock{-1};
    std::atomic<bool> is_connected{false};
    std::atomic<bool> running{true};
    std::mutex send_mutex;
    std::thread connector_thread;

    UserManager& user_manager;
    GroupManager& group_manager;
    FileManager& file_manager;

    void connection_worker();
    bool send_raw_unlocked(const std::string& payload);

public:
    TrackerSync(const std::string& ip, int port, 
                UserManager& um, GroupManager& gm, FileManager& fm);
    ~TrackerSync();

    void start();
    void stop();
    
    // Sends a live sync event if connected; ignores if offline.
    void send_sync_message(const std::string& payload);
    bool connected() const { return is_connected.load(); }
    void trigger_state_dump();
};