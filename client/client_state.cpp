#include "client_state.h"

std::string ClientState::make_key(const std::string& group_id, const std::string& filename) const {
    return group_id + "::" + filename;
}

bool ClientState::add_seeded_file(const std::string& group_id, const std::string& filename, const FileMetadata& meta) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    std::string map_key = make_key(group_id, filename);

    LocalFileInfo file_info;
    file_info.group_id = group_id;
    file_info.filename = filename;
    file_info.filepath = meta.file_path;
    file_info.file_size = meta.total_size;
    file_info.num_chunks = meta.num_chunks;
    file_info.file_hash = meta.file_hash;
    file_info.piece_hashes = meta.piece_hashes;
    // Seeder owns 100% of the pieces from the beginning
    file_info.piece_availability = std::vector<bool>(meta.num_chunks, true);
    file_info.is_fully_downloaded = true;

    tracked_files[map_key] = file_info;
    return true;
}

bool ClientState::init_download_file(const std::string& group_id, const std::string& filename,
                                     const std::string& dest_path, size_t file_size,
                                     const std::string& file_hash, const std::vector<std::string>& piece_hashes) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    std::string key = make_key(group_id, filename);

    int num_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    LocalFileInfo info;
    info.group_id = group_id;
    info.filename = filename;
    info.filepath = dest_path;
    info.file_size = file_size;
    info.num_chunks = num_chunks;
    info.file_hash = file_hash;
    info.piece_hashes = piece_hashes;
    // Downloader starts with 0 pieces available
    info.piece_availability = std::vector<bool>(num_chunks, false);
    info.is_fully_downloaded = false;

    tracked_files[key] = info;
    return true;
}

bool ClientState::mark_piece_available(const std::string& group_id, const std::string& filename, int chunk_index) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    std::string key = make_key(group_id, filename);

    auto it = tracked_files.find(key);
    if (it == tracked_files.end()) return false;

    if (chunk_index < 0 || chunk_index >= it->second.num_chunks) return false;

    it->second.piece_availability[chunk_index] = true;

    // Check if all pieces have been downloaded
    bool complete = true;
    for (bool piece_present : it->second.piece_availability) {
        if (!piece_present) {
            complete = false;
            break;
        }
    }
    it->second.is_fully_downloaded = complete;
    return true;
}

bool ClientState::has_piece(const std::string& group_id, const std::string& filename, int chunk_index) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::string key = make_key(group_id, filename);

    auto it = tracked_files.find(key);
    if (it == tracked_files.end()) return false;

    if (chunk_index < 0 || chunk_index >= it->second.num_chunks) return false;

    return it->second.piece_availability[chunk_index];
}

std::vector<bool> ClientState::get_piece_vector(const std::string& group_id, const std::string& filename) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::string key = make_key(group_id, filename);

    auto it = tracked_files.find(key);
    if (it == tracked_files.end()) return {};

    return it->second.piece_availability;
}

std::string ClientState::get_file_path(const std::string& group_id, const std::string& filename) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::string key = make_key(group_id, filename);

    auto it = tracked_files.find(key);
    if (it == tracked_files.end()) return "";

    return it->second.filepath;
}

bool ClientState::remove_file(const std::string& group_id, const std::string& filename) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    std::string key = make_key(group_id, filename);
    return tracked_files.erase(key) > 0;
}

int ClientState::get_chunk_count(const std::string& group_id, const std::string& filename) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::string key = make_key(group_id, filename);

    auto it = tracked_files.find(key);
    if (it == tracked_files.end()) return 0;

    return it->second.num_chunks;
}

std::vector<LocalFileInfo> ClientState::get_all_files() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::vector<LocalFileInfo> list;
    list.reserve(tracked_files.size());
    for (const auto& pair : tracked_files) {
        list.push_back(pair.second);
    }
    return list;
}

bool ClientState::is_file_tracked(const std::string& group_id, const std::string& filename) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::string key = make_key(group_id, filename);
    
    // If it is in the map, it is either actively downloading or fully completed.
    return tracked_files.find(key) != tracked_files.end();
}


void ClientState::clear_all() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    tracked_files.clear();
}