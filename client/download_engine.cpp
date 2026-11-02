#include "download_engine.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

#include "../common/network_utils.h"
#include "../common/protocol.h"
#include "../common/string_utils.h"

// Helper to compute SHA1 of a binary buffer
std::string compute_chunk_hash(const std::vector<char>& buffer, ssize_t length) {
    unsigned char file_hash_value[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(buffer.data()), length, file_hash_value);
    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)file_hash_value[i];
    }
    return ss.str();
}

DownloadEngine::DownloadEngine(const std::string& gid, const std::string& fname, const std::string& dest,
                               size_t size, const std::string& hash, const std::vector<std::string>& p_hashes,
                               const std::vector<PeerEndpoint>& peer_list, ClientState& state)
    : group_id(gid), filename(fname), dest_path(dest), file_size(size), 
      full_hash(hash), expected_piece_hashes(p_hashes), peers(peer_list), client_state(state) {}

void DownloadEngine::start_download() {
    // 1. Pre-allocate the file using POSIX calls
    int fd = open(dest_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) {
        std::cerr << "Error: Could not create destination file.\n";
        return;
    }
    ftruncate(fd, file_size);
    close(fd);

    // 2. Initialize local tracking so the background PeerServer knows we are leeching
    client_state.init_download_file(group_id, filename, dest_path, file_size, full_hash, expected_piece_hashes);

    int total_chunks = expected_piece_hashes.size();
    
    // 3. Create the thread-safe greedy dispatcher on the heap
    DownloadTask* download_job = new DownloadTask(group_id, filename, total_chunks);

    // 4. Spawn a worker thread for every peer in the swarm
    for (const auto& peer : peers) {
        std::thread worker(peer_worker, peer, group_id, filename, dest_path, 
                           total_chunks, expected_piece_hashes, download_job, &client_state);
        worker.detach(); // Allow concurrent execution in the background
    }
    
    std::cout << "[Client] Parallel download engine started with " << peers.size() << " peers.\n";
}

void DownloadEngine::peer_worker(PeerEndpoint peer, std::string group_id, std::string filename, 
                                 std::string dest_path, int total_chunks, 
                                 std::vector<std::string> piece_hashes, 
                                 DownloadTask* task, ClientState* state) {
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(peer.port);
    inet_pton(AF_INET, peer.ip.c_str(), &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return;
    }

    std::string bitfield_req = serialize_args({group_id, filename});
    if (!send_command(sock, CMD_REQ_BITFIELD, bitfield_req)) {
        close(sock); return;
    }

    CommandType cmd;
    std::string peer_bitfield;
    if (!recv_command(sock, cmd, peer_bitfield) || cmd != CMD_SEND_BITFIELD) {
        close(sock); return;
    }

    int fd = open(dest_path.c_str(), O_WRONLY);
    if (fd < 0) {
        close(sock); return;
    }

    int consecutive_misses = 0;
    // --- ADDED: Polling Thresholds ---
    const int poll_interval = 10;  // 10 misses * 500ms = 5 seconds between refreshes
    const int max_starvation = 60; // 60 misses * 500ms = 30 seconds to drop dead peer

    while (!task->is_complete()) {
        int chunk_index = task->get_next_piece(peer_bitfield);
        
        if (chunk_index == -1) {
            consecutive_misses++;
            
            // --- ADDED: TIER 1 - Periodic Polling ---
            if (consecutive_misses % poll_interval == 0) {
                std::string refresh_req = serialize_args({group_id, filename});
                if (send_command(sock, CMD_REQ_BITFIELD, refresh_req)) {
                    CommandType refresh_cmd;
                    std::string new_bitfield;
                    if (recv_command(sock, refresh_cmd, new_bitfield) && refresh_cmd == CMD_SEND_BITFIELD) {
                        // Ensure the bitfield length is safe before overwriting
                        if (new_bitfield.length() == (size_t)total_chunks) {
                            peer_bitfield = new_bitfield; 
                        }
                    } else {
                        break; // Peer disconnected or sent garbage
                    }
                } else {
                    break; // Peer socket broke
                }
            }

            // --- ADDED: TIER 2 - Max Starvation ---
            if (consecutive_misses >= max_starvation) {
                break; // Peer hasn't provided anything useful for 30s. Exit gracefully.
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // Reset starvation counter upon finding valid work
        consecutive_misses = 0;

        std::string req_payload = serialize_args({group_id, filename, std::to_string(chunk_index)});
        if (!send_command(sock, CMD_REQ_PIECE, req_payload)) {
            task->mark_failed(chunk_index);
            break;
        }

        CommandType res_cmd;
        std::string chunk_data;
        if (!recv_command(sock, res_cmd, chunk_data) || res_cmd != CMD_SEND_PIECE) {
            task->mark_failed(chunk_index);
            if (res_cmd == CMD_PIECE_NOT_FOUND && chunk_index >= 0 && (size_t)chunk_index < peer_bitfield.size()) {
                peer_bitfield[chunk_index] = '0'; 
            }
            continue; 
        }

        std::vector<char> buffer(chunk_data.begin(), chunk_data.end());
        std::string computed_hash = compute_chunk_hash(buffer, buffer.size());

        if (computed_hash == piece_hashes[chunk_index]) {
            off_t offset = (off_t)chunk_index * CHUNK_SIZE;
            if (lseek(fd, offset, SEEK_SET) >= 0) {
                if (write(fd, buffer.data(), buffer.size()) == (ssize_t)buffer.size()) {
                    task->mark_completed(chunk_index);
                    state->mark_piece_available(group_id, filename, chunk_index);

                    if (task->is_complete()) {
                        std::cout << "\n[Client] File '" << filename << "' downloaded successfully!\n> ";
                        std::cout.flush(); 
                    }
                } else {
                    task->mark_failed(chunk_index); 
                }
            } else {
                task->mark_failed(chunk_index);
            }
        } else {
            std::cerr << "\n[Warning] Corrupted chunk " << chunk_index << " from " << peer.ip << "\n> ";
            task->mark_failed(chunk_index);
            peer_bitfield[chunk_index] = '0'; 
        }
    }

    close(fd);
    close(sock);
}