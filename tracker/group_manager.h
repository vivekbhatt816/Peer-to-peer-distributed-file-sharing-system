#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <vector>

struct Group {
    std::string owner_id;
    std::unordered_set<std::string> members;
    std::unordered_set<std::string> pending_requests;
};

class GroupManager {
private:
    // Maps group_id to the Group struct
    std::unordered_map<std::string, Group> groups;
    
    // Reader-Writer lock for thread-safe concurrent access
    mutable std::shared_mutex rw_mutex;

public:
    // Core group operations
    bool is_member(const std::string& group_id, const std::string& user_id);
    std::string create_group(const std::string& group_id, const std::string& current_user);
    std::string join_group(const std::string& group_id, const std::string& current_user);
    std::string leave_group(const std::string& group_id, const std::string& current_user);
    std::string list_groups();

    // Owner-exclusive operations
    std::string list_requests(const std::string& group_id, const std::string& current_user);
    std::string accept_request(const std::string& group_id, const std::string& target_user, const std::string& current_user);

    void sync_create_group(const std::string& group_id, const std::string& owner_id);
    void sync_join_group(const std::string& group_id, const std::string& user_id);
    void sync_accept_request(const std::string& group_id, const std::string& user_id);
    void sync_leave_group(const std::string& group_id, const std::string& user_id);
    std::vector<std::string> get_full_state();
};