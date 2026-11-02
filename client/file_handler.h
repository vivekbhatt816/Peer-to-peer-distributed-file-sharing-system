#pragma once

#include <string>
#include <vector>

constexpr size_t CHUNK_SIZE = 524288; // 512KB[cite: 1]

struct FileMetadata {
    std::string file_path;
    size_t total_size;
    int num_chunks;
    std::string file_hash;                 // SHA1 hash of the entire file
    std::vector<std::string> piece_hashes; // SHA1 hashes of each 512KB piece
};

FileMetadata get_file_metadata(const std::string& filepath);

// Updated to return the fully populated metadata struct
FileMetadata process_file_chunks(const std::string& filepath);