#pragma once

#include <string>
#include <vector>

// Packs a vector of arguments into a single space-separated string
std::string serialize_args(const std::vector<std::string>& args);

// Unpacks a space-separated string into a vector of individual arguments
std::vector<std::string> deserialize_args(const std::string& payload);