#include "peer_server.h"
#include <iostream>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include "../common/network_utils.h"
#include "../common/string_utils.h"

PeerServer::PeerServer(const std::string& ip, int port, ClientState& state)
    : ip(ip), port(port), client_state(state), is_running(false), server_sock(-1) {}

PeerServer::~PeerServer() {
    stop();
}

void PeerServer::start() {
    is_running.store(true);
    listener_thread = std::thread(&PeerServer::accept_loop, this);
}

void PeerServer::stop() {
    if (!is_running.exchange(false)) return; // Prevent double-stopping

    // 1. Force all active peer transfers to abort gracefully
    {
        std::lock_guard<std::mutex> lock(peers_mutex);
        for (int peer_socket : active_peers) {
            shutdown(peer_socket, SHUT_RDWR);
        }
    }

    // 2. Wait for all detached worker threads to finish their cleanup
    while (true) {
        {
            std::lock_guard<std::mutex> lock(peers_mutex);
            if (active_peers.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 3. Unblock the accept loop
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }

    if (listener_thread.joinable()) {
        listener_thread.join();
    }
    std::cout << "[PeerServer] Shut down gracefully.\n";
}

void PeerServer::accept_loop() {
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef __APPLE__
    setsockopt(server_sock, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
    #endif

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[PeerServer] Bind failed on " << ip << ":" << port << "\n";
        return;
    }
    
    if (listen(server_sock, 20) < 0) {
        std::cerr << "[PeerServer] Listen failed.\n";
        return;
    }

    while (is_running.load()) {
        struct sockaddr_in peer_addr{};
        socklen_t peer_len = sizeof(peer_addr);
        int peer_sock = accept(server_sock, (struct sockaddr*)&peer_addr, &peer_len);

        if (peer_sock < 0) {
            if (!is_running.load()) break;
            continue;
        }

        // Register the new socket before handing it off
        {
            std::lock_guard<std::mutex> lock(peers_mutex);
            active_peers.insert(peer_sock);
        }

        // Pass 'this' so the static function can access the tracking set
        std::thread worker(handle_peer, this, peer_sock, std::ref(client_state));
        worker.detach();
    }
}

void PeerServer::handle_peer(PeerServer* server, int peer_sock, ClientState& state) {
    CommandType command_type;
    std::string payload;

    while (recv_command(peer_sock, command_type, payload)) {
        if (command_type == CMD_REQ_PIECE) {
            std::vector<std::string> args = deserialize_args(payload);
            if (args.size() != 3) {
                send_command(peer_sock, CMD_PIECE_NOT_FOUND, "Invalid arguments");
                continue;
            }

            std::string group_id = args[0];
            std::string filename = args[1];
            int chunk_index = std::stoi(args[2]);

            if (!state.has_piece(group_id, filename, chunk_index)) {
                send_command(peer_sock, CMD_PIECE_NOT_FOUND, "");
                continue;
            }

            std::string filepath = state.get_file_path(group_id, filename);
            int fd = open(filepath.c_str(), O_RDONLY);
            if (fd < 0) {
                send_command(peer_sock, CMD_PIECE_NOT_FOUND, "File not accessible");
                continue;
            }

            off_t offset = (off_t)chunk_index * CHUNK_SIZE;
            if (lseek(fd, offset, SEEK_SET) < 0) {
                close(fd);
                send_command(peer_sock, CMD_PIECE_NOT_FOUND, "Seek failed");
                continue;
            }

            std::vector<char> buffer(CHUNK_SIZE);
            ssize_t bytes_read = read(fd, buffer.data(), CHUNK_SIZE);
            close(fd);

            if (bytes_read > 0) {
                std::string binary_payload(buffer.data(), bytes_read);
                if (!send_command(peer_sock, CMD_SEND_PIECE, binary_payload)) {
                    break; // Peer likely disconnected mid-transfer
                }
            } else {
                send_command(peer_sock, CMD_PIECE_NOT_FOUND, "Read error");
            }
        }
        else if (command_type == CMD_REQ_BITFIELD) {
            std::vector<std::string> args = deserialize_args(payload);
            if (args.size() != 2) {
                // If arguments are malformed, we can just send an empty bitfield
                send_command(peer_sock, CMD_SEND_BITFIELD, "");
                continue;
            }

            std::string group_id = args[0];
            std::string filename = args[1];

            // 1. Fetch the thread-safe boolean array from local memory
            std::vector<bool> pieces = state.get_piece_vector(group_id, filename);

            // 2. Serialize the boolean array into a string of 1s and 0s
            std::string bitfield_str = "";
            for (bool has_piece : pieces) {
                bitfield_str += (has_piece ? "1" : "0");
            }

            // 3. Send the map back to the downloading peer
            if (!send_command(peer_sock, CMD_SEND_BITFIELD, bitfield_str)) {
                break; // Peer disconnected
            }
        }
    }

    // Cleanup when the connection ends or is forcefully shut down
    close(peer_sock);
    {
        std::lock_guard<std::mutex> lock(server->peers_mutex);
        server->active_peers.erase(peer_sock);
    }
}