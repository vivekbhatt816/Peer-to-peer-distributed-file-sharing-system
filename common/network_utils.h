#pragma once

#include <cstddef>
#include <string>
#include "protocol.h"

bool send_all(int sockfd, const void* buffer, size_t len);

bool recv_all(int sockfd, void* buffer, size_t len);

bool send_command(int sockfd, CommandType cmd, const std::string& payload);

bool recv_command(int sockfd, CommandType& cmd, std::string& payload);