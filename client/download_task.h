#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <random>    // ADDED: For Shuffled Deck MT19937 engine
#include <numeric>   // ADDED: For std::iota
#include <algorithm> // ADDED: For std::shuffle

class DownloadTask {
private:
    std::string group_id;
    std::string filename;
    int total_chunks;
    
    // 0 = missing, 1 = in_progress, 2 = completed
    std::vector<int> piece_status; 
    
    // ADDED: The "Deck of Cards" to ensure randomized chunk selection
    std::vector<int> piece_order; 
    std::mutex task_mutex;

public:
    DownloadTask(const std::string& gid, const std::string& fname, int chunks);

    // Greedily finds the next available piece a peer can provide using the shuffled deck
    int get_next_piece(const std::string& peer_bitfield);

    // Called if a download fails or the hash is invalid
    void mark_failed(int chunk_index);

    // Called when a piece is successfully written and hashed
    void mark_completed(int chunk_index);

    // Checks if all pieces are state '2'
    bool is_complete();
};