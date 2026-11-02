#include "file_handler.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <openssl/sha.h> // Requires compiling with -lcrypto

// Helper function to convert 20-byte binary SHA1 hash to a 40-character hex string
std::string sha1_to_hex(const unsigned char* hash) {
    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

FileMetadata get_file_metadata(const std::string& filepath) {
    FileMetadata file_metadata_obj;
    file_metadata_obj.file_path = filepath;
    file_metadata_obj.total_size = 0;
    file_metadata_obj.num_chunks = 0;

    struct stat file_stat;
    if (stat(filepath.c_str(), &file_stat) < 0) {
        std::cerr << "Error: Could not locate file " << filepath << "\n";
        return file_metadata_obj;
    }

    file_metadata_obj.total_size = file_stat.st_size;
    file_metadata_obj.num_chunks = (file_metadata_obj.total_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    return file_metadata_obj;
}

FileMetadata process_file_chunks(const std::string& filepath) {
    FileMetadata meta = get_file_metadata(filepath);
    if (meta.total_size == 0) return meta;

    int file_fd_handle = open(filepath.c_str(), O_RDONLY);
    if (file_fd_handle < 0) {
        std::cerr << "Error: Could not open file " << filepath << "\n";
        return meta;
    }

    std::vector<char> buffer(CHUNK_SIZE);
    ssize_t bytes_read;
    
    // Initialize OpenSSL context for the continuous full-file hash
    SHA_CTX full_file_ctx;
    SHA1_Init(&full_file_ctx);

    while ((bytes_read = read(file_fd_handle, buffer.data(), CHUNK_SIZE)) > 0) {
        // 1. Calculate hash for this specific 512KB piece
        unsigned char piece_hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(buffer.data()), bytes_read, piece_hash);
        meta.piece_hashes.push_back(sha1_to_hex(piece_hash));

        // 2. Feed the chunk data into the running full-file hash context
        SHA1_Update(&full_file_ctx, buffer.data(), bytes_read);
    }

    if (bytes_read < 0) {
        std::cerr << "Error: Failed to read from file " << filepath << "\n";
    }

    // Finalize the full file hash
    unsigned char final_file_hash[SHA_DIGEST_LENGTH];
    SHA1_Final(final_file_hash, &full_file_ctx);
    meta.file_hash = sha1_to_hex(final_file_hash);

    close(file_fd_handle);
    return meta;
}