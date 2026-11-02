#include "file_manager.h"
#include "sync_protocol.h"
#include <sstream>

std::string FileManager::upload_file(const std::string& group_id, const std::string& file_name, 
                                     size_t file_size, const std::string& file_hash, 
                                     const std::string& piece_hashes, const std::string& current_user) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    auto& files_in_group = group_files[group_id];
    
    if (files_in_group.find(file_name) != files_in_group.end()) {
        files_in_group[file_name].seeders.insert(current_user);
        return "User added as a seeder to existing file '" + file_name + "'";
    }
    
    FileInfo file_record;
    file_record.name = file_name;
    file_record.group_id = group_id;
    file_record.size = file_size;
    file_record.hash = file_hash;
    file_record.piece_hashes = piece_hashes; // ADDED
    file_record.seeders.insert(current_user);
    
    files_in_group[file_name] = file_record;
    return "File '" + file_name + "' successfully registered in group '" + group_id + "'";
}

std::string FileManager::list_files(const std::string& group_id) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = group_files.find(group_id);
    if (it == group_files.end() || it->second.empty()) {
        return "No files available in group '" + group_id + "'";
    }
    
    std::string result_list = "Available Files in '" + group_id + "':\n";
    for (const auto& pair : it->second) {
        result_list += "- " + pair.first + " (" + std::to_string(pair.second.size) + " bytes)\n";
    }
    if (!result_list.empty() && result_list.back() == '\n') result_list.pop_back();
    return result_list;
}

std::string FileManager::download_file(const std::string& group_id, const std::string& filename, 
                                       UserManager& user_manager, const std::string& current_user) {
    
    std::unique_lock<std::shared_mutex> lock(rw_mutex);

    auto group_it = group_files.find(group_id);
    if (group_it == group_files.end()) {
        return "Error: Group not found";
    }

    auto file_it = group_it->second.find(filename);
    if (file_it == group_it->second.end()) {
        return "Error: File not found in group";
    }

    // 2. Remove 'const' so we can modify the seeders set
    FileInfo& file = file_it->second;

    std::string response = std::to_string(file.size) + " " + file.hash + " " + file.piece_hashes;

    bool seeder_found = false;
    for (const std::string& seeder_id : file.seeders) {
        std::string ip;
        int port;
        if (user_manager.get_user_endpoint(seeder_id, ip, port)) {
            response += " " + ip + ":" + std::to_string(port);
            seeder_found = true;
        }
    }

    if (!seeder_found) {
        return "Error: No active seeders currently online for this file";
    }

    // 3. Officially add the downloading user to the swarm
    file.seeders.insert(current_user);

    return response;
}

std::string FileManager::stop_share(const std::string& group_id, const std::string& file_name, 
                                    const std::string& current_user) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);

    auto git = group_files.find(group_id);
    if (git == group_files.end()) return "Error: Group not found";

    auto fit = git->second.find(file_name);
    if (fit == git->second.end()) return "Error: File not found in group";

    auto sit = fit->second.seeders.find(current_user);
    if (sit == fit->second.seeders.end()) return "Error: You are not seeding this file";

    fit->second.seeders.erase(sit);

    if (fit->second.seeders.empty()) {
        git->second.erase(fit);
        return "Stopped sharing. No remaining seeders; file metadata removed from group.";
    }

    return "Successfully stopped sharing file '" + file_name + "'";
}

void FileManager::sync_upload_file(const std::string& group_id, const std::string& file_name, 
                                   size_t file_size, const std::string& file_hash, 
                                   const std::string& piece_hashes, const std::string& seeder_id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    auto& files_in_group = group_files[group_id];
    
    if (files_in_group.find(file_name) == files_in_group.end()) {
        FileInfo new_file;
        new_file.name = file_name;
        new_file.group_id = group_id;
        new_file.size = file_size;
        new_file.hash = file_hash;
        new_file.piece_hashes = piece_hashes; // ADDED
        files_in_group[file_name] = new_file;
    }
    files_in_group[file_name].seeders.insert(seeder_id);
}

void FileManager::sync_stop_share(const std::string& group_id, const std::string& file_name, const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    auto git = group_files.find(group_id);
    if (git != group_files.end()) {
        auto fit = git->second.find(file_name);
        if (fit != git->second.end()) {
            fit->second.seeders.erase(user_id);
            if (fit->second.seeders.empty()) {
                git->second.erase(fit);
            }
        }
    }
}

std::vector<std::string> FileManager::get_full_state() {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::vector<std::string> state;
    
    for (const auto& group_pair : group_files) {
        for (const auto& file_pair : group_pair.second) {
            const FileInfo& info = file_pair.second;
            for (const auto& seeder : info.seeders) {
                // Ensure format_sync_file_upload in sync_protocol.h accepts the 6th argument
                state.push_back(format_sync_file_upload(
                    info.group_id, info.name, info.size, info.hash, info.piece_hashes, seeder
                ));
            }
        }
    }
    return state;
}