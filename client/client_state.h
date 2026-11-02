#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include "file_handler.h"

struct LocalFileInfo {
    std::string group_id;
    std::string filename;
    std::string filepath;
    size_t file_size;
    int num_chunks;
    std::string file_hash;
    std::vector<std::string> piece_hashes;
    std::vector<bool> piece_availability;
    bool is_fully_downloaded;
};

class ClientState {
private:
    // Key format: "group_id::filename"
    std::unordered_map<std::string, LocalFileInfo> tracked_files;
    mutable std::shared_mutex rw_mutex;

    std::string make_key(const std::string& group_id, const std::string& filename) const;

public:
    ClientState() = default;

    // Registers a local file seeded by this client (all pieces marked true)
    bool add_seeded_file(const std::string& group_id, const std::string& filename, const FileMetadata& meta);

    // Registers a file starting to download (all pieces initialized to false)
    bool init_download_file(const std::string& group_id, const std::string& filename,
                            const std::string& dest_path, size_t file_size,
                            const std::string& file_hash, const std::vector<std::string>& piece_hashes);

    // Marks a specific chunk index as downloaded and verified
    bool mark_piece_available(const std::string& group_id, const std::string& filename, int chunk_index);

    // Checks if the client holds a specific chunk
    bool has_piece(const std::string& group_id, const std::string& filename, int chunk_index) const;

    // Returns a copy of the piece availability bitset for peer querying
    std::vector<bool> get_piece_vector(const std::string& group_id, const std::string& filename) const;

    // Retrieves local path on disk to read data from during seeding
    std::string get_file_path(const std::string& group_id, const std::string& filename) const;

    // Stops sharing a file and removes it from local tracking
    bool remove_file(const std::string& group_id, const std::string& filename);

    // Returns the total number of chunks for a file
    int get_chunk_count(const std::string& group_id, const std::string& filename) const;

    // Returns a list of all tracked file records (useful for show_downloads)
    std::vector<LocalFileInfo> get_all_files() const;

    void clear_all();

    // Add to public methods
    bool is_file_tracked(const std::string& group_id, const std::string& filename);
};