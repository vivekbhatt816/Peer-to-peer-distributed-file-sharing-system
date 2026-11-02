#include "tracker_sync.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include "../common/string_utils.h"

TrackerSync::TrackerSync(const std::string& ip, int port, 
                         UserManager& um, GroupManager& gm, FileManager& fm)
    : peer_ip(ip), peer_port(port), 
      user_manager(um), group_manager(gm), file_manager(fm) {}

TrackerSync::~TrackerSync() {
    stop();
}

void TrackerSync::start() {
    connector_thread = std::thread(&TrackerSync::connection_worker, this);
}

void TrackerSync::stop() {
    running.store(false);
    
    int sock = peer_sock.exchange(-1);
    if (sock >= 0) close(sock);
    is_connected.store(false);

    if (connector_thread.joinable()) {
        connector_thread.join();
    }
}

void TrackerSync::trigger_state_dump() {
    if (!is_connected.load()) return;
    std::lock_guard<std::mutex> lock(send_mutex);
    
    for (const auto& sync_payload : user_manager.get_full_state()) {
        if (!send_raw_unlocked(sync_payload)) return; 
    }
    for (const auto& payload : group_manager.get_full_state()) {
        if (!send_raw_unlocked(payload)) return;
    }
    for (const auto& payload : file_manager.get_full_state()) {
        if (!send_raw_unlocked(payload)) return;
    }
}

void TrackerSync::connection_worker() {
    while (running.load()) {
        if (!is_connected.load()) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock >= 0) {
                struct sockaddr_in peer_addr{};
                peer_addr.sin_family = AF_INET;
                peer_addr.sin_port = htons(peer_port);
                inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr);

                if (connect(sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) == 0) {
                    
                    // 1. Set a 2-second timeout so recv_command doesn't block infinitely on a zombie socket
                    struct timeval tv;
                    tv.tv_sec = 2;
                    tv.tv_usec = 0;
                    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

                    // 2. Perform the Application-Layer Handshake
                    std::string ping_payload = serialize_args({"PING"});
                    CommandType cmd;
                    std::string sync_result;
                    
                    // 3. Wait for the C++ application to actually reply
                    if (send_command(sock, CMD_SYNC, ping_payload) && 
                        recv_command(sock, cmd, sync_result) && sync_result == "SYNC_ACK") {
                        
                        // The application is alive! Remove the timeout for normal operations
                        tv.tv_sec = 0; 
                        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

                        peer_sock.store(sock);
                        is_connected.store(true);
                        std::cout << "[Sync] Connected to peer tracker at " 
                                  << peer_ip << ":" << peer_port << ". Initiating state dump...\n";
                        trigger_state_dump();
                        // // --- THE FULL STATE DUMP ---
                        // std::lock_guard<std::mutex> lock(send_mutex);

                        // for (const auto& payload : user_manager.get_full_state()) {
                        //     if (!send_raw_unlocked(payload)) break; 
                        // }
                        // for (const auto& payload : group_manager.get_full_state()) {
                        //     if (!send_raw_unlocked(payload)) break;
                        // }
                        // for (const auto& payload : file_manager.get_full_state()) {
                        //     if (!send_raw_unlocked(payload)) break;
                        // }
                        
                        std::cout << "[Sync] Full state dump complete.\n";
                    } else {
                        // Zombie socket or handshake failed. Close it and try again later.
                        close(sock);
                    }
                } else {
                    close(sock);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

bool TrackerSync::send_raw_unlocked(const std::string& payload) {
    int sock = peer_sock.load();
    if (sock < 0) return false;
    
    if (!send_command(sock, CMD_SYNC, payload)) {
        std::cerr << "[Sync] Peer connection dropped.\n";
        close(sock);
        peer_sock.store(-1);
        is_connected.store(false);
        return false;
    }
    return true;
}

void TrackerSync::send_sync_message(const std::string& payload) {
    if (!is_connected.load()) return; 

    // Clients lock the wire for a single message
    std::lock_guard<std::mutex> lock(send_mutex);
    send_raw_unlocked(payload);
}



// #include "tracker_sync.h"
// #include <iostream>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <unistd.h>
// #include <chrono>

// TrackerSync::TrackerSync(const std::string& ip, int port, 
//                          UserManager& um, GroupManager& gm, FileManager& fm)
//     : peer_ip(ip), peer_port(port), 
//       user_manager(um), group_manager(gm), file_manager(fm) {}

// TrackerSync::~TrackerSync() {
//     stop();
// }

// void TrackerSync::start() {
//     connector_thread = std::thread(&TrackerSync::connection_worker, this);
// }

// void TrackerSync::stop() {
//     running.store(false);
    
//     int sock = peer_sock.exchange(-1);
//     if (sock >= 0) close(sock);
//     is_connected.store(false);

//     if (connector_thread.joinable()) {
//         connector_thread.join();
//     }
// }

// void TrackerSync::connection_worker() {
//     while (running.load()) {
//         if (!is_connected.load()) {
//             int sock = socket(AF_INET, SOCK_STREAM, 0);
//             if (sock >= 0) {
//                 struct sockaddr_in peer_addr{};
//                 peer_addr.sin_family = AF_INET;
//                 peer_addr.sin_port = htons(peer_port);
//                 inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr);

//                 if (connect(sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) == 0) {
//                     peer_sock.store(sock);
//                     is_connected.store(true);
//                     std::cout << "[Sync] Connected to peer tracker at " 
//                               << peer_ip << ":" << peer_port << ". Initiating state dump...\n";
                    
//                     // --- THE FULL STATE DUMP ---
//                     // 1. Dump Users & Sessions
//                     std::lock_guard<std::mutex> lock(send_mutex);

//                     for (const auto& payload : user_manager.get_full_state()) {
//                         if (!send_raw_unlocked(payload)) break; 
//                     }
//                     for (const auto& payload : group_manager.get_full_state()) {
//                         if (!send_raw_unlocked(payload)) break;
//                     }
//                     for (const auto& payload : file_manager.get_full_state()) {
//                         if (!send_raw_unlocked(payload)) break;
//                     }
                    
//                     std::cout << "[Sync] Full state dump complete.\n";
//                 } else {
//                     close(sock);
//                 }
//             }
//         }
//         std::this_thread::sleep_for(std::chrono::seconds(2));
//     }
// }

// bool TrackerSync::send_raw_unlocked(const std::string& payload) {
//     int sock = peer_sock.load();
//     if (sock < 0) return false;
    
//     if (!send_command(sock, CMD_SYNC, payload)) {
//         std::cerr << "[Sync] Peer connection dropped.\n";
//         close(sock);
//         peer_sock.store(-1);
//         is_connected.store(false);
//         return false;
//     }
//     return true;
// }

// void TrackerSync::send_sync_message(const std::string& payload) {
//     if (!is_connected.load()) return; 

//     // Clients lock the wire for a single message
//     std::lock_guard<std::mutex> lock(send_mutex);
//     send_raw_unlocked(payload);
// }