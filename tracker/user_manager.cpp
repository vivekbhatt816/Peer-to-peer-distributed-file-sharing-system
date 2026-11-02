#include "user_manager.h"
#include "sync_protocol.h"

std::string UserManager::create_user(const std::string& user_id, const std::string& password) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    
    if (users.find(user_id) != users.end()) {
        return "Error: User already exists";
    }
    
    users[user_id] = {password, false, "", 0};
    return "User " + user_id + " created successfully";
}

std::string UserManager::login(const std::string& user_id, const std::string& password, 
                               const std::string& ip, int port) {
    std::unique_lock<std::shared_mutex> guard(rw_mutex);
    
    auto user_iter = users.find(user_id);
    if (user_iter == users.end()) {
        return "Error: User does not exist";
    }
    
    if (user_iter->second.password != password) {
        return "Error: Incorrect password";
    }
    
    if (user_iter->second.is_logged_in) {
        return "Error: User already logged in";
    }
    
    user_iter->second.is_logged_in = true;
    user_iter->second.ip = ip;
    user_iter->second.port = port;
    return "Login successful";
}

std::string UserManager::logout(const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    
    auto it = users.find(user_id);
    if (it != users.end() && it->second.is_logged_in) {
        it->second.is_logged_in = false;
        it->second.ip = "";
        it->second.port = 0;
        return "Logout successful";
    }
    return "Error: User not logged in";
}

bool UserManager::get_user_endpoint(const std::string& user_id, std::string& out_ip, int& out_port) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    
    auto it = users.find(user_id);
    if (it != users.end() && it->second.is_logged_in) {
        out_ip = it->second.ip;
        out_port = it->second.port;
        return true;
    }
    return false;
}

void UserManager::sync_user(const std::string& user_id, const std::string& password) {
    // Unique lock: We are writing to memory
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    
    // Blindly insert or update the user. Logged-in state is always false for a synced user
    // because TCP sessions don't transfer across trackers.
    if (users.find(user_id) == users.end()) {
        users[user_id] = {password, false, "", 0};
    }
}

void UserManager::sync_login(const std::string& user_id, const std::string& ip, int port) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    // Tracker 1 already verified the password, so Tracker 2 just blindly updates the session
    if (users.find(user_id) != users.end()) {
        users[user_id].is_logged_in = true;
        users[user_id].ip = ip;
        users[user_id].port = port;
    }
}

void UserManager::sync_logout(const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    if (users.find(user_id) != users.end()) {
        users[user_id].is_logged_in = false;
        users[user_id].ip = "";
        users[user_id].port = 0;
    }
}

std::vector<std::string> UserManager::get_full_state() {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::vector<std::string> state;
    
    for (const auto& pair : users) {
        // 1. Dump the account creation
        state.push_back(format_sync_user(pair.first, pair.second.password));
        
        // 2. If the user is currently online, dump their active session (IP/Port)
        if (pair.second.is_logged_in) {
            state.push_back(format_sync_login(pair.first, pair.second.ip, pair.second.port));
        }
    }
    return state;
}