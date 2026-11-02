#include "download_task.h"

DownloadTask::DownloadTask(const std::string& gid, const std::string& fname, int chunks)
    : group_id(gid), filename(fname), total_chunks(chunks) {
    // Initialize all pieces as missing (0)
    piece_status.assign(total_chunks, 0);

    // --- ADDED: Generate and Shuffle the Deck ---
    piece_order.resize(total_chunks);
    std::iota(piece_order.begin(), piece_order.end(), 0); // Fill with 0, 1, 2... chunks-1

    std::random_device random_device_obj;
    std::mt19937 rng_engine(random_device_obj());
    std::shuffle(piece_order.begin(), piece_order.end(), rng_engine);
    // ------------------------------------------
}

int DownloadTask::get_next_piece(const std::string& peer_bitfield) {
    std::lock_guard<std::mutex> lock(task_mutex);

    // Safety check: ensure peer sent a valid length bitfield
    if (peer_bitfield.length() != (size_t)total_chunks) {
        return -1; 
    }

    // --- UPDATED: Draw from the Shuffled Deck ---
    for (int i : piece_order) {
        if (piece_status[i] == 0 && peer_bitfield[i] == '1') {
            piece_status[i] = 1; // Lock it as in-progress so other threads ignore it
            return i;
        }
    }
    
    return -1; // No pieces available from this peer right now
}

void DownloadTask::mark_failed(int chunk_index) {
    std::lock_guard<std::mutex> lock(task_mutex);
    if (chunk_index >= 0 && chunk_index < total_chunks) {
        // Revert to missing so another thread can greedily pick it up later
        piece_status[chunk_index] = 0; 
    }
}

void DownloadTask::mark_completed(int chunk_index) {
    std::lock_guard<std::mutex> lock(task_mutex);
    if (chunk_index >= 0 && chunk_index < total_chunks) {
        piece_status[chunk_index] = 2; // Mark as permanently complete
    }
}

bool DownloadTask::is_complete() {
    std::lock_guard<std::mutex> lock(task_mutex);
    for (int status : piece_status) {
        if (status != 2) return false;
    }
    return true;
}