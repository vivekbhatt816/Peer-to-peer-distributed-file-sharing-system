#include "string_utils.h"
#include <sstream>

std::string serialize_args(const std::vector<std::string>& args) {
    std::string parsed_args;
    for (size_t i = 0; i < args.size(); ++i) {
        parsed_args += args[i];
        if (i < args.size() - 1) {
            parsed_args += " "; // Space delimiter
        }
    }
    return parsed_args;
}

std::vector<std::string> deserialize_args(const std::string& payload) {
    std::vector<std::string> args;
    std::istringstream iss(payload);
    std::string current_token;
    
    // Automatically splits the string by whitespace
    while (iss >> current_token) {
        args.push_back(current_token);
    }
    
    return args;
}