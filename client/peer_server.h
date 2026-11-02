#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <set>
#include <mutex>
#include "client_state.h"

class PeerServer {
private:
    std::string ip;
    int port;
    ClientState& client_state;
    std::atomic<bool> is_running;
    int server_sock;
    std::thread listener_thread;

    // Tracking for graceful shutdown
    std::set<int> active_peers;
    std::mutex peers_mutex;

    void accept_loop();
    
    // Updated to accept a pointer to the server instance
    static void handle_peer(PeerServer* server, int peer_sock, ClientState& state);

public:
    PeerServer(const std::string& ip, int port, ClientState& state);
    ~PeerServer();

    void start();
    void stop();
};