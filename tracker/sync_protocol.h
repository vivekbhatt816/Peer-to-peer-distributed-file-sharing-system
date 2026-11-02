#pragma once

#include <string>
#include <vector>
#include "../common/string_utils.h"

namespace SyncAction {
    const std::string USER         = "USER";
    const std::string GROUP_CREATE = "GROUP_CREATE";
    const std::string GROUP_JOIN   = "GROUP_JOIN";
    const std::string GROUP_ACCEPT = "GROUP_ACCEPT";
    const std::string GROUP_LEAVE = "GROUP_LEAVE";
    const std::string FILE_UPLOAD  = "FILE_UPLOAD";
    const std::string STOP_SHARE   = "STOP_SHARE";
    const std::string LOGIN = "LOGIN";
    const std::string LOGOUT = "LOGOUT";
}

inline std::string format_sync_login(const std::string& user_id, const std::string& ip, int port) {
    return serialize_args({SyncAction::LOGIN, user_id, ip, std::to_string(port)});
}

inline std::string format_sync_logout(const std::string& user_id) {
    return serialize_args({SyncAction::LOGOUT, user_id});
}

inline std::string format_sync_user(const std::string& user_id, const std::string& password) {
    return serialize_args({SyncAction::USER, user_id, password});
}

inline std::string format_sync_group_create(const std::string& group_id, const std::string& owner_id) {
    return serialize_args({SyncAction::GROUP_CREATE, group_id, owner_id});
}

inline std::string format_sync_group_join(const std::string& group_id, const std::string& user_id) {
    return serialize_args({SyncAction::GROUP_JOIN, group_id, user_id});
}

inline std::string format_sync_group_accept(const std::string& group_id, const std::string& user_id) {
    return serialize_args({SyncAction::GROUP_ACCEPT, group_id, user_id});
}

inline std::string format_sync_group_leave(const std::string& group_id, const std::string& user_id) {
    return serialize_args({SyncAction::GROUP_LEAVE, group_id, user_id});
}

// UPDATED: Added piece_hashes to the signature and the serialization vector
inline std::string format_sync_file_upload(const std::string& group_id, const std::string& file_name,
                                          size_t file_size, const std::string& file_hash, 
                                          const std::string& piece_hashes, const std::string& seeder_id) {
    return serialize_args({SyncAction::FILE_UPLOAD, group_id, file_name, 
                           std::to_string(file_size), file_hash, piece_hashes, seeder_id});
}

inline std::string format_sync_stop_share(const std::string& group_id, const std::string& file_name,
                                         const std::string& user_id) {
    return serialize_args({SyncAction::STOP_SHARE, group_id, file_name, user_id});
}