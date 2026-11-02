#pragma once

#include <cstdint>

enum CommandType : uint16_t {
    CMD_RESPONSE = 0,
    CMD_CREATE_USER = 1,
    CMD_LOGIN = 2,
    CMD_CREATE_GROUP = 3,
    CMD_JOIN_GROUP = 4,
    CMD_LEAVE_GROUP = 5,
    CMD_LIST_GROUPS = 6,
    CMD_LIST_REQUESTS = 7,
    CMD_ACCEPT_REQUEST = 8,
    CMD_LOGOUT = 9,
    CMD_UPLOAD_FILE = 10,
    CMD_LIST_FILES = 11,
    CMD_DOWNLOAD_FILE = 12,
    CMD_STOP_SHARE = 13,
    CMD_GET_PIECE = 14, 
    CMD_SYNC = 15,

   
    CMD_REQ_PIECE = 20,       
    CMD_SEND_PIECE = 21,      
    CMD_PIECE_NOT_FOUND = 22,  
    CMD_REQ_BITFIELD = 23,    
    CMD_SEND_BITFIELD = 24
};

struct __attribute__((packed)) MessageHeader {
    uint16_t command_type;
    uint32_t payload_length;
};