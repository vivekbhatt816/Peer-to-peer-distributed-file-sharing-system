#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "../common/network_utils.h"
#include "../common/string_utils.h"
#include "file_handler.h"
#include "client_state.h"
#include "peer_server.h"
#include "download_engine.h"
#include <csignal>

// ADDED: Structure to hold tracker endpoints cleanly
struct TrackerEndpoint {
    std::string ip;
    int port;
};

// ADDED: Reusable function to hunt for a surviving tracker
// UPDATED: Added failed_index to ban the crashed tracker
int connect_to_tracker(const std::vector<TrackerEndpoint>& trackers, int& connected_index, int failed_index = -1) {
    for (size_t i = 0; i < trackers.size(); ++i) {
        // Skip the tracker we already know is dead
        if ((int)i == failed_index) {
            continue; 
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        struct sockaddr_in tracker_addr{};
        tracker_addr.sin_family = AF_INET;
        tracker_addr.sin_port = htons(trackers[i].port);
        inet_pton(AF_INET, trackers[i].ip.c_str(), &tracker_addr.sin_addr);

        std::cout << "[Client] Attempting connection to Tracker " << (i + 1) 
                  << " (" << trackers[i].ip << ":" << trackers[i].port << ")...\n";

        if (connect(sock, (struct sockaddr*)&tracker_addr, sizeof(tracker_addr)) == 0) {
            connected_index = i;
            return sock; 
        }
        
        close(sock);
    }
    return -1; 
}

// Helper function to tokenize user input by spaces
std::vector<std::string> tokenize_input(const std::string& command_line) {
    std::vector<std::string> command_tokens;
    std::istringstream stream(command_line);
    std::string token;
    while (stream >> token) {
        command_tokens.push_back(token);
    }
    return command_tokens;
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    if (argc != 3) {
        std::cerr << "Usage: ./client <IP>:<PORT> tracker_info.txt\n";
        return 1;
    }

    // Parse the client's own P2P endpoint (IP:PORT)
    std::string client_endpoint = argv[1];
    size_t colon_pos = client_endpoint.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "Error: Client address must be in <IP>:<PORT> format.\n";
        return 1;
    }
    std::string client_ip = client_endpoint.substr(0, colon_pos);
    int client_port = std::stoi(client_endpoint.substr(colon_pos + 1));

    // Parse tracker_info.txt dynamically
    std::string tracker_info_file = argv[2];
    std::ifstream infile(tracker_info_file);
    if (!infile.is_open()) {
        std::cerr << "Error: Could not open " << tracker_info_file << "\n";
        return 1;
    }

    std::vector<TrackerEndpoint> trackers;
    std::string t_ip;
    int t_port;
    while (infile >> t_ip >> t_port) {
        trackers.push_back({t_ip, t_port});
    }
    infile.close();

    if (trackers.empty()) {
        std::cerr << "Error: tracker_info.txt contains no valid trackers.\n";
        return 1;
    }

    std::cout << "Client booted successfully.\n";
    std::cout << "P2P Server configured for " << client_ip << ":" << client_port << " (Starts on login)\n";

    // Call our new wrapper to find an active tracker
    int active_tracker_index = -1;
    int tracker_sock = connect_to_tracker(trackers, active_tracker_index);

    if (tracker_sock < 0) {
        std::cerr << "Fatal Error: All trackers are currently offline. Exiting.\n";
        return 1;
    }

    std::cout << "Successfully connected to active Tracker at " 
              << trackers[active_tracker_index].ip << ":" << trackers[active_tracker_index].port << "\n";

    // Initialize the thread-safe local state registry
    ClientState client_state;

    PeerServer peer_server(client_ip, client_port, client_state);

    // CLI Loop Variables
    std::string input;
    bool is_logged_in = false;
    std::string current_user_id = "";
    std::string current_password = "";

    std::cout << "> ";
    while (std::getline(std::cin, input)) {
        if (input.empty()) {
            std::cout << "> ";
            continue;
        }

        std::vector<std::string> tokens = tokenize_input(input);
        std::string command = tokens[0];

        // Local Session Validation
        bool needs_auth = (command != "create_user" && command != "login" && command != "quit");
        if (needs_auth && !is_logged_in) {
            std::cerr << "Error: You must be logged in to perform '" << command << "'.\n";
            std::cout << "> ";
            continue;
        }

        CommandType cmd_type = CMD_RESPONSE; // Default invalid state
        std::vector<std::string> payload_args;
        bool skip_network = false;
        bool should_quit = false;

        if (command == "quit") {
            should_quit = true;
            if (is_logged_in) {
                cmd_type = CMD_LOGOUT;
                payload_args = {};
            } else {
                skip_network = true; // Nothing to tell tracker, just quit
            }
        }

        else if (command == "logout") {
            cmd_type = CMD_LOGOUT;
            payload_args = {};
        }

        // --- Standard Proxy Commands ---
        if (command == "create_user" && tokens.size() == 3) {
            cmd_type = CMD_CREATE_USER;
            payload_args = {tokens[1], tokens[2]};
        } 
        else if (command == "login" && tokens.size() == 3) {
            // Block login if already logged in
            if (is_logged_in) {
                std::cerr << "Error: You are already logged in as " << current_user_id << ". Please logout first.\n";
                skip_network = true;
            } else {
                cmd_type = CMD_LOGIN;
                payload_args = {tokens[1], tokens[2], std::to_string(client_port)};
            }
        } 
        else if (command == "create_group" && tokens.size() == 2) {
            cmd_type = CMD_CREATE_GROUP;
            payload_args = {tokens[1]};
        }
        else if (command == "join_group" && tokens.size() == 2) {
            cmd_type = CMD_JOIN_GROUP;
            payload_args = {tokens[1]};
        }
        else if (command == "leave_group" && tokens.size() == 2) {
            cmd_type = CMD_LEAVE_GROUP;
            payload_args = {tokens[1]};
        }
        else if (command == "list_groups" && tokens.size() == 1) {
            cmd_type = CMD_LIST_GROUPS;
            payload_args = {};
        }
        else if (command == "list_requests" && tokens.size() == 2) {
            cmd_type = CMD_LIST_REQUESTS;
            payload_args = {tokens[1]};
        }
        else if (command == "accept_request" && tokens.size() == 3) {
            cmd_type = CMD_ACCEPT_REQUEST;
            payload_args = {tokens[1], tokens[2]};
        }
        else if (command == "list_files" && tokens.size() == 2) {
            cmd_type = CMD_LIST_FILES;
            payload_args = {tokens[1]};
        }

        // --- Complex Local State Commands ---
        else if (command == "upload_file" && tokens.size() == 3) {
            std::string group_id = tokens[1];
            std::string file_path = tokens[2];

            FileMetadata meta = process_file_chunks(file_path);
            if (meta.total_size == 0) {
                std::cout << "> "; 
                continue; 
            }

            size_t pos = file_path.find_last_of("/\\");
            std::string filename = (pos == std::string::npos) ? file_path : file_path.substr(pos + 1);

            client_state.add_seeded_file(group_id, filename, meta);

            // Serialize the piece hashes into a single string (e.g., "hash1,hash2,hash3")
            std::string combined_piece_hashes = "";
            for (size_t i = 0; i < meta.piece_hashes.size(); ++i) {
                combined_piece_hashes += meta.piece_hashes[i];
                if (i != meta.piece_hashes.size() - 1) {
                    combined_piece_hashes += ",";
                }
            }

            cmd_type = CMD_UPLOAD_FILE;
            payload_args = {group_id, filename, std::to_string(meta.total_size), meta.file_hash, combined_piece_hashes};
        }
        else if (command == "stop_share" && tokens.size() == 3) {
            cmd_type = CMD_STOP_SHARE;
            payload_args = {tokens[1], tokens[2]};
            
            client_state.remove_file(tokens[1], tokens[2]);
            std::cout << "[Client] Removed '" << tokens[2] << "' from local seeding registry.\n";
        }
        else if (command == "show_downloads" && tokens.size() == 1) {
            skip_network = true;
            auto files = client_state.get_all_files();
            if (files.empty()) {
                std::cout << "No active or completed downloads.\n";
            } else {
                for (const auto& f : files) {
                    if (f.is_fully_downloaded) {
                        std::cout << "[C] [" << f.group_id << "] " << f.filename << "\n";
                    } else {
                        std::cout << "[D] [" << f.group_id << "] " << f.filename << "\n";
                    }
                }
            }
        }
        else if (command == "download_file" && tokens.size() == 4) {
            std::string group_id = tokens[1];
            std::string filename = tokens[2];
            std::string dest_path = tokens[3];

            // Prevent re-downloading existing or currently downloading files
            if (client_state.is_file_tracked(group_id, filename)) {
                std::cout << "[Client] You are already downloading or seeding '" << filename << "'.\n";
                skip_network = true;
            } else {
                cmd_type = CMD_DOWNLOAD_FILE;
                payload_args = {group_id, filename}; 
            }
        }
        else {
            std::cerr << "Error: Invalid command or argument count.\n";
            skip_network = true;
        }

        // --- Network Transmission & Response Handling ---
        // --- Network Transmission & Response Handling ---
        if (!skip_network) {
            std::string serialized_payload = serialize_args(payload_args);
            bool request_successful = false;
            int attempts = 0;

            while (attempts < 2) {
                if (send_command(tracker_sock, cmd_type, serialized_payload)) {
                    CommandType response_cmd;
                    std::string response_payload;
                    if (recv_command(tracker_sock, response_cmd, response_payload)) {
                        
                        // --- SUCCESS BLOCK ---
                        if (command != "download_file" || response_payload.find("Error") != std::string::npos) {
                            std::cout << response_payload << "\n";
                        }

                        if (command == "login" && response_payload == "Login successful") {
                            is_logged_in = true;
                            current_user_id = tokens[1];
                            current_password = tokens[2]; // Cache password for failover
                            std::cout << "[Client] Starting P2P server on " << client_ip << ":" << client_port << "...\n";
                            peer_server.start();
                        }
                        else if (command == "logout" || command == "quit") {
                            is_logged_in = false;
                            current_user_id = "";
                            current_password = "";
                            client_state.clear_all(); 
                            
                            if (command == "logout") {
                                std::cout << "[Client] Stopping P2P server...\n";
                                peer_server.stop();
                            }
                        }
                        else if (command == "download_file" && response_payload.find("Error") == std::string::npos) {
                            std::istringstream iss(response_payload);
                            std::string size_str, hash_str, piece_hashes_str;
                            
                            if (iss >> size_str >> hash_str >> piece_hashes_str) {
                                size_t file_size = std::stoull(size_str);
                                
                                std::vector<std::string> expected_piece_hashes;
                                std::stringstream ph_stream(piece_hashes_str);
                                std::string ph;
                                while (std::getline(ph_stream, ph, ',')) expected_piece_hashes.push_back(ph);

                                std::vector<PeerEndpoint> peers;
                                std::string peer_str;
                                while (iss >> peer_str) {
                                    size_t colon = peer_str.find(':');
                                    if (colon != std::string::npos) {
                                        PeerEndpoint ep;
                                        ep.ip = peer_str.substr(0, colon);
                                        ep.port = std::stoi(peer_str.substr(colon + 1));
                                        if (!(ep.ip == client_ip && ep.port == client_port)) peers.push_back(ep);
                                    }
                                }

                                if (peers.empty()) {
                                    std::cout << "[Client] You are the only active seeder. Waiting for others to join swarm.\n";
                                } else {
                                    std::string dest_path = tokens[3];
                                    DownloadEngine engine(tokens[1], tokens[2], dest_path, file_size, 
                                                          hash_str, expected_piece_hashes, peers, client_state);
                                    engine.start_download();
                                }
                            }
                        }
                        
                        request_successful = true;
                        break; // Exit the retry loop on success
                    }
                }

                // --- FAILOVER INTERCEPTION ---
                attempts++;
                if (attempts >= 2) break; // Maximum retries reached

                std::cerr << "\n[Warning] Connection to primary tracker lost. Attempting failover...\n";
                close(tracker_sock); // Kill the dead socket
                
                // Dynamically hunt for the surviving tracker
                int crashed_tracker = active_tracker_index;
                tracker_sock = connect_to_tracker(trackers, active_tracker_index, crashed_tracker);
                
                if (tracker_sock < 0) {
                    std::cerr << "[Error] All trackers are offline.\n";
                    break; 
                }

                std::cout << "[Client] Failover successful. Connected to Tracker at " 
                          << trackers[active_tracker_index].ip << ":" << trackers[active_tracker_index].port << "\n";

                // STEP 3: Silent State Restoration
                if (is_logged_in) {
                    std::cout << "[Client] Restoring session state...\n";
                    std::string login_payload = serialize_args({current_user_id, current_password, std::to_string(client_port)});
                    
                    // Send login command silently in the background
                    send_command(tracker_sock, CMD_LOGIN, login_payload);
                    CommandType dummy_cmd;
                    std::string dummy_res;
                    recv_command(tracker_sock, dummy_cmd, dummy_res); 
                    
                    // Because trackers sync data via CMD_SYNC, the secondary tracker recognizes 
                    // the credentials and silently accepts the login, binding this new socket.
                }
                
                std::cout << "[Client] Retrying command...\n";
                // The loop restarts, cleanly transmitting the user's original command to Tracker 2.
            }

            if (!request_successful) {
                std::cerr << "Fatal Error: Network connection unrecoverable.\n";
                break;
            }
        }
        if (should_quit) {
            break; 
        }

        std::cout << "> ";
    }
    
    peer_server.stop();
    close(tracker_sock);
    return 0;
}