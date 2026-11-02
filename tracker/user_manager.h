#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <vector>

struct User {
    std::string password;
    bool is_logged_in;
    std::string ip;
    int port;
};

class UserManager {
private:
    std::unordered_map<std::string, User> users;
    mutable std::shared_mutex rw_mutex;

public:
    std::string create_user(const std::string& user_id, const std::string& password);
    // Updated: Accept client's IP and peer-to-peer listening port on login
    std::string login(const std::string& user_id, const std::string& password, const std::string& ip, int port);
    std::string logout(const std::string& user_id);

    // New: Look up a seeder's endpoint (IP:Port)
    bool get_user_endpoint(const std::string& user_id, std::string& out_ip, int& out_port);

    void sync_user(const std::string& user_id, const std::string& password);
    void sync_login(const std::string& user_id, const std::string& ip, int port);
    void sync_logout(const std::string& user_id);
    std::vector<std::string> get_full_state();
};