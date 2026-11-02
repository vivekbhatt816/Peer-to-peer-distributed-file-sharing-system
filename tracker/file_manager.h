#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <vector>
#include "user_manager.h"

struct FileInfo {
    std::string name;
    std::string group_id;
    size_t size;
    std::string hash;
    std::string piece_hashes; // ADDED: Stores the comma-separated piece hashes
    std::unordered_set<std::string> seeders;
};

class FileManager {
private:
    std::unordered_map<std::string, std::unordered_map<std::string, FileInfo>> group_files;
    mutable std::shared_mutex rw_mutex;

public:
    // Updated signature
    std::string upload_file(const std::string& group_id, const std::string& file_name, 
                            size_t file_size, const std::string& file_hash, 
                            const std::string& piece_hashes, const std::string& current_user);
    
    std::string list_files(const std::string& group_id);

   
    std::string download_file(const std::string& group_id, const std::string& file_name, 
                             UserManager& user_manager, const std::string& current_user);

    std::string stop_share(const std::string& group_id, const std::string& file_name, 
                           const std::string& current_user);

    // Updated signature
    void sync_upload_file(const std::string& group_id, const std::string& file_name, 
                          size_t file_size, const std::string& file_hash, 
                          const std::string& piece_hashes, const std::string& seeder_id);

    void sync_stop_share(const std::string& group_id, const std::string& file_name, const std::string& user_id);
    
    std::vector<std::string> get_full_state();
};