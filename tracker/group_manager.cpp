#include "group_manager.h"
#include "sync_protocol.h"
#include <sstream>

bool GroupManager::is_member(const std::string& group_id, const std::string& user_id) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = groups.find(group_id);
    if (it == groups.end()) return false;
    return it->second.members.find(user_id) != it->second.members.end();
}

std::string GroupManager::create_group(const std::string& group_id, const std::string& current_user) {
    // Exclusive write lock
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    
    if (groups.find(group_id) != groups.end()) {
        return "Error: Group '" + group_id + "' already exists";
    }
    
    // Initialize the group
    Group group_record;
    group_record.owner_id = current_user;
    group_record.members.insert(current_user); // The owner is automatically a member
    
    groups[group_id] = group_record;
    
    return "Group '" + group_id + "' created successfully";
}

std::string GroupManager::join_group(const std::string& group_id, const std::string& current_user) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    
    auto it = groups.find(group_id);
    if (it == groups.end()) {
        return "Error: Group '" + group_id + "' does not exist";
    }
    
    if (it->second.members.find(current_user) != it->second.members.end()) {
        return "Error: You are already a member of this group";
    }
    
    if (it->second.pending_requests.find(current_user) != it->second.pending_requests.end()) {
        return "Error: You already have a pending request for this group";
    }
    
    it->second.pending_requests.insert(current_user);
    return "Join request sent for group '" + group_id + "'";
}

std::string GroupManager::leave_group(const std::string& group_id, const std::string& current_user) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    
    auto it = groups.find(group_id);
    if (it == groups.end()) {
        return "Error: Group '" + group_id + "' does not exist";
    }
    
    if (it->second.members.find(current_user) == it->second.members.end()) {
        return "Error: You are not a member of this group";
    }
    
    if (it->second.owner_id == current_user) {
        // Depending on assignment rules, the owner might not be allowed to leave without deleting the group.
        // We will prevent the owner from leaving to maintain group integrity.
        return "Error: Owner cannot leave the group. You must stop sharing/delete instead.";
    }
    
    it->second.members.erase(current_user);
    return "Successfully left group '" + group_id + "'";
}

std::string GroupManager::list_groups() {
    // Shared read lock: multiple threads can list groups simultaneously
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    
    if (groups.empty()) {
        return "No groups available";
    }
    
    std::string member_list = "Available Groups:\n";
    for (const auto& pair : groups) {
        member_list += "- " + pair.first + "\n";
    }
    
    // Remove the trailing newline for clean output
    if (!member_list.empty() && member_list.back() == '\n') {
        member_list.pop_back();
    }
    return member_list;
}

std::string GroupManager::list_requests(const std::string& group_id, const std::string& current_user) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    
    auto it = groups.find(group_id);
    if (it == groups.end()) {
        return "Error: Group '" + group_id + "' does not exist";
    }
    
    // Access validation: Only the owner can view pending requests
    if (it->second.owner_id != current_user) {
        return "Error: You are not the owner of this group";
    }
    
    if (it->second.pending_requests.empty()) {
        return "No pending requests for group '" + group_id + "'";
    }
    
    std::string result = "Pending Requests:\n";
    for (const auto& user : it->second.pending_requests) {
        result += "- " + user + "\n";
    }
    
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

std::string GroupManager::accept_request(const std::string& group_id, const std::string& target_user, const std::string& current_user) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    
    auto it = groups.find(group_id);
    if (it == groups.end()) {
        return "Error: Group '" + group_id + "' does not exist";
    }
    
    // Access validation: Only the owner can accept requests
    if (it->second.owner_id != current_user) {
        return "Error: You are not the owner of this group";
    }
    
    auto req_it = it->second.pending_requests.find(target_user);
    if (req_it == it->second.pending_requests.end()) {
        return "Error: User '" + target_user + "' does not have a pending request";
    }
    
    // Perform the transfer
    it->second.pending_requests.erase(req_it);
    it->second.members.insert(target_user);
    
    return "User '" + target_user + "' has been added to group '" + group_id + "'";
}

void GroupManager::sync_create_group(const std::string& group_id, const std::string& owner_id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    if (groups.find(group_id) == groups.end()) {
        Group new_group;
        new_group.owner_id = owner_id;
        new_group.members.insert(owner_id);
        groups[group_id] = new_group;
    }
}

void GroupManager::sync_join_group(const std::string& group_id, const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    groups[group_id].pending_requests.insert(user_id);
}

void GroupManager::sync_accept_request(const std::string& group_id, const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    groups[group_id].pending_requests.erase(user_id); // Erase does nothing if not present
    groups[group_id].members.insert(user_id);
}

void GroupManager::sync_leave_group(const std::string& group_id, const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    auto it = groups.find(group_id);
    if (it != groups.end()) {
        it->second.members.erase(user_id);
    }
}

std::vector<std::string> GroupManager::get_full_state() {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    std::vector<std::string> state;
    
    for (const auto& group_pair : groups) {
        const std::string& gid = group_pair.first;
        const auto& grp = group_pair.second;
        
        // 1. Dump group creation
        state.push_back(format_sync_group_create(gid, grp.owner_id));
        
        // 2. Dump pending join requests
        for (const auto& pending_user : grp.pending_requests) {
            state.push_back(format_sync_group_join(gid, pending_user));
        }
        
        // 3. Dump accepted members (skip the owner as they are added on creation)
        for (const auto& member : grp.members) {
            if (member != grp.owner_id) {
                state.push_back(format_sync_group_accept(gid, member));
            }
        }
    }
    return state;
}