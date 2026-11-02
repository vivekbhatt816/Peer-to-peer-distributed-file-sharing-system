#include "network_utils.h"

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cerrno>
#include <vector>

bool send_all(int sockfd, const void* buffer, size_t len) {
    size_t sent_total = 0;
    const char* buf_ptr = static_cast<const char*>(buffer);

    while (sent_total < len) {
        ssize_t bytes_sent = send(sockfd, buf_ptr + sent_total, len - sent_total, 0);

        if (bytes_sent == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent_total += static_cast<size_t>(bytes_sent);
    }
    return true;
}

bool recv_all(int sockfd, void* buffer, size_t len) {
    size_t received_total = 0;
    char* buf_ptr = static_cast<char*>(buffer);

    while (received_total < len) {
        ssize_t bytes_read = recv(sockfd, buf_ptr + received_total, len - received_total, 0);

        if (bytes_read == 0) {
            return false;
        }
        if (bytes_read == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        received_total += static_cast<size_t>(bytes_read);
    }
    return true;
}

bool send_command(int sockfd, CommandType cmd, const std::string& payload) {
    MessageHeader header;
    header.command_type = htons(static_cast<uint16_t>(cmd));
    header.payload_length = htonl(static_cast<uint32_t>(payload.size()));

    if (!send_all(sockfd, &header, sizeof(header))) {
        return false;
    }

    if (!payload.empty()) {
        if (!send_all(sockfd, payload.data(), payload.size())) {
            return false;
        }
    }
    return true;
}

bool recv_command(int sockfd, CommandType& cmd, std::string& payload) {
    MessageHeader header;
    if (!recv_all(sockfd, &header, sizeof(header))) {
        return false;
    }

    cmd = static_cast<CommandType>(ntohs(header.command_type));
    uint32_t payload_len = ntohl(header.payload_length);

    payload.clear();
    if (payload_len > 0) {
        std::vector<char> buffer(payload_len);
        if (!recv_all(sockfd, buffer.data(), payload_len)) {
            return false;
        }
        payload.assign(buffer.data(), payload_len);
    }
    return true;
}