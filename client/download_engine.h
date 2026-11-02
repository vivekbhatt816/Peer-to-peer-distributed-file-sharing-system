#pragma once

#include <string>
#include <vector>
#include <thread>
#include "download_task.h"
#include "client_state.h"

struct PeerEndpoint {
    std::string ip;
    int port;
};

class DownloadEngine {
private:
    std::string group_id;
    std::string filename;
    std::string dest_path;
    size_t file_size;
    std::string full_hash;
    std::vector<std::string> expected_piece_hashes;
    std::vector<PeerEndpoint> peers;
    ClientState& client_state;

    // Worker function executed by each detached std::thread
    static void peer_worker(PeerEndpoint peer, std::string group_id, std::string filename, 
                            std::string dest_path, int total_chunks, 
                            std::vector<std::string> piece_hashes, 
                            DownloadTask* task, ClientState* state);

public:
    DownloadEngine(const std::string& gid, const std::string& fname, const std::string& dest,
                   size_t size, const std::string& hash, const std::vector<std::string>& p_hashes,
                   const std::vector<PeerEndpoint>& peer_list, ClientState& state);

    // Initializes the file on disk and launches the swarm threads
    void start_download();
};