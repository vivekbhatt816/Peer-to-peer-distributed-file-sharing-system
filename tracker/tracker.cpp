#include <iostream>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <atomic>
#include <set>
#include <mutex>


#include "../common/network_utils.h"
#include "../common/string_utils.h"
#include "user_manager.h"
#include "group_manager.h"
#include "file_manager.h"
#include "tracker_sync.h"
#include "sync_protocol.h"
#include "async_logger.h" 
#include <csignal>

std::atomic<bool> is_running{true};
std::set<int> active_clients;
std::mutex clients_mutex;

void admin_console(int server_sock, TrackerSync& tracker_sync);
void handle_client(int client_sock, UserManager& user_manager, GroupManager& group_manager, 
                   FileManager& file_manager, TrackerSync& tracker_sync);

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    if (argc != 3) {
        std::cerr << "Usage: ./tracker tracker_info.txt tracker_no\n";
        return 1;
    }

    std::string tracker_info_file = argv[1];
    int tracker_no;
    
    try {
        tracker_no = std::stoi(argv[2]);
    } catch (const std::exception& e) {
        std::cerr << "Error: tracker_no must be an integer.\n";
        return 1;
    }

    std::ifstream config_stream(tracker_info_file);
    if (!config_stream.is_open()) {
        std::cerr << "Error: Could not open " << tracker_info_file << "\n";
        return 1;
    }

    std::string ip1, ip2;
    int port1 = 0, port2 = 0;

    if (!(config_stream >> ip1 >> port1) || !(config_stream >> ip2 >> port2)) {
        std::cerr << "Error: tracker_info.txt must contain at least 2 entries.\n";
        return 1;
    }
    config_stream.close();

    std::string my_ip, peer_ip;
    int my_port = 0, peer_port = 0;

    if (tracker_no == 1) {
        my_ip = ip1; my_port = port1;
        peer_ip = ip2; peer_port = port2;
    } else if (tracker_no == 2) {
        my_ip = ip2; my_port = port2;
        peer_ip = ip1; peer_port = port1;
    } else {
        std::cerr << "Error: tracker_no must be 1 or 2.\n";
        return 1;
    }

    global_logger.log("Booting Tracker " + std::to_string(tracker_no) + " on " + my_ip + ":" + std::to_string(my_port));
    global_logger.log("Target Peer Tracker configured at " + peer_ip + ":" + std::to_string(peer_port));

    UserManager global_user_manager;
    GroupManager global_group_manager;
    FileManager global_file_manager;

    TrackerSync tracker_sync(peer_ip, peer_port, global_user_manager, global_group_manager, global_file_manager);
    tracker_sync.start();

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef __APPLE__
    setsockopt(server_sock, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
    #endif

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(my_port);
    inet_pton(AF_INET, my_ip.c_str(), &server_addr.sin_addr);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed. Is the port in use?\n";
        return 1;
    }
    if (listen(server_sock, 10) < 0) {
        std::cerr << "Listen failed.\n";
        return 1;
    }

    global_logger.log("Tracker listening on " + my_ip + ":" + std::to_string(my_port));
    global_logger.log("Type 'quit' at any time to safely shut down the tracker.");

    std::thread admin_thread(admin_console, server_sock, std::ref(tracker_sync));
    admin_thread.detach();

    while (is_running.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock < 0) {
            if (!is_running.load()) break;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            active_clients.insert(client_sock);
        }

        std::thread client_thread(handle_client, client_sock, 
                                  std::ref(global_user_manager), 
                                  std::ref(global_group_manager),
                                  std::ref(global_file_manager),
                                  std::ref(tracker_sync));
        client_thread.detach(); 
    }

    global_logger.log("Tracker main thread shut down successfully.");
    return 0;
}

void admin_console(int server_sock, TrackerSync& tracker_sync) {
    std::string command;
    while (std::getline(std::cin, command)) {
        if (command == "quit") {
            global_logger.log("[ADMIN] Initiating graceful shutdown. Notifying clients...");
            is_running.store(false);

            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                for (int client_sock : active_clients) {
                    shutdown(client_sock, SHUT_RDWR);
                }
            }

            while (true) {
                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    if (active_clients.empty()) break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            global_logger.log("[ADMIN] All clients cleaned up. Terminating sync...");
            tracker_sync.stop();
            close(server_sock); 
            break;
        }
    }
}

void handle_client(int client_sock, UserManager& user_manager, GroupManager& group_manager, 
                   FileManager& file_manager, TrackerSync& tracker_sync) {
    global_logger.log("[SYSTEM] Client session started on socket " + std::to_string(client_sock));
    
    CommandType cmd;
    std::string payload;
    std::string current_user = ""; 

    while (true) {
        if (!recv_command(client_sock, cmd, payload)) {
            if (!current_user.empty()) {
                user_manager.logout(current_user);
                tracker_sync.send_sync_message(format_sync_logout(current_user));
                global_logger.log("[CLIENT] User '" + current_user + "' unexpectedly disconnected.");
            }
            global_logger.log("[SYSTEM] Client on socket " + std::to_string(client_sock) + " disconnected.");
            break;
        }

        std::vector<std::string> args = deserialize_args(payload);
        std::string reply_message;

        bool needs_auth = (cmd >= CMD_CREATE_GROUP && cmd != CMD_SYNC);
        if (needs_auth && current_user.empty()) {
            reply_message = "Error: You must be logged in to perform this action";
        } 
        else {
            switch (cmd) {
                case CMD_SYNC: {
                    if (args.empty()) break;
                    std::string action = args[0];
                    global_logger.log("[SYNC-IN] Received action: " + action + " from peer tracker.");

                    if (action == "PING") {
                        // Manually reply ONLY to the PING to satisfy the zombie-socket handshake.
                        // The 'continue' at the end of the loop will still suppress all other sync acks.
                        send_command(client_sock, CMD_RESPONSE, "SYNC_ACK");
                        tracker_sync.trigger_state_dump();
                        break; 
                    }
                    
                    if (action == SyncAction::USER && args.size() == 3) {
                        user_manager.sync_user(args[1], args[2]);
                    } else if (action == SyncAction::LOGIN && args.size() == 4) {
                        user_manager.sync_login(args[1], args[2], std::stoi(args[3]));
                    } else if (action == SyncAction::LOGOUT && args.size() == 2) {
                        user_manager.sync_logout(args[1]);
                    } else if (action == SyncAction::GROUP_CREATE && args.size() == 3) {
                        group_manager.sync_create_group(args[1], args[2]);
                    } else if (action == SyncAction::GROUP_JOIN && args.size() == 3) {
                        group_manager.sync_join_group(args[1], args[2]);
                    } else if (action == SyncAction::GROUP_ACCEPT && args.size() == 3) {
                        group_manager.sync_accept_request(args[1], args[2]);
                    } else if (action == SyncAction::GROUP_LEAVE && args.size() == 3) {
                        group_manager.sync_leave_group(args[1], args[2]);
                    } 
                    // UPDATED size to 7
                    else if (action == SyncAction::FILE_UPLOAD && args.size() == 7) { 
                        file_manager.sync_upload_file(args[1], args[2], std::stoull(args[3]), args[4], args[5], args[6]);
                    } 
                    else if (action == SyncAction::STOP_SHARE && args.size() == 4) {
                        file_manager.sync_stop_share(args[1], args[2], args[3]);
                    }
                    
                    reply_message = "SYNC_ACK";
                    break;
                }
                case CMD_CREATE_USER:
                    if (args.size() == 2) {
                        reply_message = user_manager.create_user(args[0], args[1]);
                        if (reply_message.find("Error") == std::string::npos) {
                            tracker_sync.send_sync_message(format_sync_user(args[0], args[1]));
                            global_logger.log("[CLIENT] Created new user: " + args[0]);
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;
                
                case CMD_LOGIN:
                    if (!current_user.empty()) {
                        reply_message = "Error: You are already logged in as " + current_user + ". Please logout first.";
                    } else if (args.size() == 3) { 
                        try {
                            int peer_port = std::stoi(args[2]);
                            struct sockaddr_in peer_addr{};
                            socklen_t peer_len = sizeof(peer_addr);
                            getpeername(client_sock, (struct sockaddr*)&peer_addr, &peer_len);
                            char client_ip[INET_ADDRSTRLEN];
                            inet_ntop(AF_INET, &peer_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

                            reply_message = user_manager.login(args[0], args[1], std::string(client_ip), peer_port);
                            if (reply_message == "Login successful") {
                                current_user = args[0];
                                tracker_sync.send_sync_message(format_sync_login(args[0], std::string(client_ip), peer_port));
                                global_logger.log("[CLIENT] User '" + current_user + "' logged in from " + std::string(client_ip) + ":" + std::to_string(peer_port));
                            }
                        } catch (...) {
                            reply_message = "Error: Invalid port format";
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_LOGOUT:
                    if (current_user.empty()) reply_message = "Error: You are not logged in";
                    else {
                        reply_message = user_manager.logout(current_user);
                        if (reply_message.find("Error") == std::string::npos) {
                            tracker_sync.send_sync_message(format_sync_logout(current_user));
                            global_logger.log("[CLIENT] User '" + current_user + "' logged out.");
                            current_user = ""; 
                        }
                    }
                    break;

                case CMD_CREATE_GROUP:
                    if (args.size() == 1) {
                        reply_message = group_manager.create_group(args[0], current_user);
                        if (reply_message.find("Error") == std::string::npos) {
                            tracker_sync.send_sync_message(format_sync_group_create(args[0], current_user));
                            global_logger.log("[CLIENT] User '" + current_user + "' created group: " + args[0]);
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_JOIN_GROUP:
                    if (args.size() == 1) {
                        reply_message = group_manager.join_group(args[0], current_user);
                        if (reply_message.find("Error") == std::string::npos) {
                            tracker_sync.send_sync_message(format_sync_group_join(args[0], current_user));
                            global_logger.log("[CLIENT] User '" + current_user + "' requested to join group: " + args[0]);
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_LEAVE_GROUP:
                    if (args.size() == 1) {
                        reply_message = group_manager.leave_group(args[0], current_user);
                        if (reply_message.find("Error") == std::string::npos) {
                            tracker_sync.send_sync_message(format_sync_group_leave(args[0], current_user));
                            global_logger.log("[CLIENT] User '" + current_user + "' left group: " + args[0]);
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_LIST_GROUPS:
                    if (args.empty()) {
                        reply_message = group_manager.list_groups();
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_LIST_REQUESTS:
                    if (args.size() == 1) {
                        reply_message = group_manager.list_requests(args[0], current_user);
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_ACCEPT_REQUEST:
                    if (args.size() == 2) {
                        reply_message = group_manager.accept_request(args[0], args[1], current_user);
                        if (reply_message.find("Error") == std::string::npos) {
                            tracker_sync.send_sync_message(format_sync_group_accept(args[0], args[1]));
                            global_logger.log("[CLIENT] User '" + current_user + "' accepted '" + args[1] + "' into group: " + args[0]);
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_UPLOAD_FILE:
                    // UPDATED size to 5
                    if (args.size() == 5) {
                        if (!group_manager.is_member(args[0], current_user)) {
                            reply_message = "Error: You are not a member of group '" + args[0] + "'";
                        } else {
                            try {
                                size_t file_size = std::stoull(args[2]);
                                // Added args[4]
                                reply_message = file_manager.upload_file(args[0], args[1], file_size, args[3], args[4], current_user);
                                if (reply_message.find("Error") == std::string::npos) {
                                    tracker_sync.send_sync_message(format_sync_file_upload(args[0], args[1], file_size, args[3], args[4], current_user));
                                    global_logger.log("[CLIENT] User '" + current_user + "' uploaded file '" + args[1] + "' to group: " + args[0]);
                                }
                            } catch (...) {
                                reply_message = "Error: Invalid file size format.";
                            }
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_LIST_FILES:
                    if (args.size() == 1) {
                        if (!group_manager.is_member(args[0], current_user)) {
                            reply_message = "Error: You are not a member of group '" + args[0] + "'";
                        } else {
                            reply_message = file_manager.list_files(args[0]);
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_DOWNLOAD_FILE:
                    if (args.size() == 2) {
                        if (!group_manager.is_member(args[0], current_user)) {
                            reply_message = "Error: You are not a member of group '" + args[0] + "'";
                        } else {
                            // 1. Pass current_user to the function
                            reply_message = file_manager.download_file(args[0], args[1], user_manager, current_user);
                            
                            // 2. If successful, sync the new seeder to Tracker 2
                            if (reply_message.find("Error") == std::string::npos) {
                                // Extract the metadata from the response string (SIZE HASH PIECE_HASHES)
                                std::istringstream iss(reply_message);
                                std::string size_str, hash_str, piece_hashes_str;
                                iss >> size_str >> hash_str >> piece_hashes_str;

                                // Broadcast to Tracker 2 that this user is now in the swarm
                                tracker_sync.send_sync_message(format_sync_file_upload(
                                    args[0], args[1], std::stoull(size_str), hash_str, piece_hashes_str, current_user
                                ));
                                
                                global_logger.log("[CLIENT] User '" + current_user + "' joined swarm for '" + args[1] + "'");
                            }
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                case CMD_STOP_SHARE:
                    if (args.size() == 2) {
                        if (!group_manager.is_member(args[0], current_user)) {
                            reply_message = "Error: You are not a member of group '" + args[0] + "'";
                        } else {
                            reply_message = file_manager.stop_share(args[0], args[1], current_user);
                            if (reply_message.find("Error") == std::string::npos) {
                                tracker_sync.send_sync_message(format_sync_stop_share(args[0], args[1], current_user));
                                global_logger.log("[CLIENT] User '" + current_user + "' stopped sharing '" + args[1] + "' in group: " + args[0]);
                            }
                        }
                    } else reply_message = "Error: Invalid arguments.";
                    break;

                default:
                    reply_message = "Error: Unknown or unimplemented command";
                    break;
            }
        }
        if(cmd==CMD_SYNC){
            continue;
        }

        if (!send_command(client_sock, CMD_RESPONSE, reply_message)) {
            if (!current_user.empty()) {
                user_manager.logout(current_user);
                tracker_sync.send_sync_message(format_sync_logout(current_user));
                global_logger.log("[CLIENT] User '" + current_user + "' unexpectedly disconnected.");
            }
            break; 
        }
    }

    close(client_sock);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        active_clients.erase(client_sock);
    }
}

// #include <iostream>
// #include <string>
// #include <thread>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <unistd.h>
// #include <vector>
// #include <fstream>
// #include <atomic>
// #include <set>
// #include <mutex>

// #include "../common/network_utils.h"
// #include "../common/string_utils.h"
// #include "user_manager.h"
// #include "group_manager.h"
// #include "file_manager.h"
// #include "tracker_sync.h"
// #include "sync_protocol.h"
// #include <csignal>

// // Global flags and thread-safe tracking for graceful shutdown
// std::atomic<bool> is_running{true};
// std::set<int> active_clients;
// std::mutex clients_mutex;

// void admin_console(int server_sock, TrackerSync& tracker_sync);
// void handle_client(int client_sock, UserManager& user_manager, GroupManager& group_manager, 
//                    FileManager& file_manager, TrackerSync& tracker_sync);

// int main(int argc, char* argv[]) {
//     signal(SIGPIPE, SIG_IGN);
//     if (argc != 3) {
//         std::cerr << "Usage: ./tracker tracker_info.txt tracker_no\n";
//         return 1;
//     }

//     std::string tracker_info_file = argv[1];
//     int tracker_no;
    
//     try {
//         tracker_no = std::stoi(argv[2]);
//     } catch (const std::exception& e) {
//         std::cerr << "Error: tracker_no must be an integer.\n";
//         return 1;
//     }

//     std::ifstream infile(tracker_info_file);
//     if (!infile.is_open()) {
//         std::cerr << "Error: Could not open " << tracker_info_file << "\n";
//         return 1;
//     }

//     std::string ip1, ip2;
//     int port1 = 0, port2 = 0;

//     if (!(infile >> ip1 >> port1) || !(infile >> ip2 >> port2)) {
//         std::cerr << "Error: tracker_info.txt must contain at least 2 entries.\n";
//         return 1;
//     }
//     infile.close();

//     std::string my_ip, peer_ip;
//     int my_port = 0, peer_port = 0;

//     if (tracker_no == 1) {
//         my_ip = ip1; my_port = port1;
//         peer_ip = ip2; peer_port = port2;
//     } else if (tracker_no == 2) {
//         my_ip = ip2; my_port = port2;
//         peer_ip = ip1; peer_port = port1;
//     } else {
//         std::cerr << "Error: tracker_no must be 1 or 2.\n";
//         return 1;
//     }

//     std::cout << "Booting Tracker " << tracker_no << " on " << my_ip << ":" << my_port << "\n";
//     std::cout << "Target Peer Tracker configured at " << peer_ip << ":" << peer_port << "\n";

//     UserManager global_user_manager;
//     GroupManager global_group_manager;
//     FileManager global_file_manager;

//     TrackerSync tracker_sync(peer_ip, peer_port, global_user_manager, global_group_manager, global_file_manager);
//     tracker_sync.start();

//     int server_sock = socket(AF_INET, SOCK_STREAM, 0);
//     int opt = 1;
//     setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
//     #ifdef __APPLE__
//     setsockopt(server_sock, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
//     #endif

//     struct sockaddr_in server_addr{};
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(my_port);
//     inet_pton(AF_INET, my_ip.c_str(), &server_addr.sin_addr);

//     if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
//         std::cerr << "Bind failed. Is the port in use?\n";
//         return 1;
//     }
//     if (listen(server_sock, 10) < 0) {
//         std::cerr << "Listen failed.\n";
//         return 1;
//     }

//     std::cout << "Tracker listening on " << my_ip << ":" << my_port << "\n";
//     std::cout << "Type 'quit' at any time to safely shut down the tracker.\n";

//     std::thread admin_thread(admin_console, server_sock, std::ref(tracker_sync));
//     admin_thread.detach();

//     while (is_running.load()) {
//         struct sockaddr_in client_addr{};
//         socklen_t client_len = sizeof(client_addr);
//         int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);

//         if (client_sock < 0) {
//             if (!is_running.load()) break;
//             continue;
//         }

//         // Add the new client to our thread-safe tracking set
//         {
//             std::lock_guard<std::mutex> lock(clients_mutex);
//             active_clients.insert(client_sock);
//         }

//         std::thread client_thread(handle_client, client_sock, 
//                                   std::ref(global_user_manager), 
//                                   std::ref(global_group_manager),
//                                   std::ref(global_file_manager),
//                                   std::ref(tracker_sync));
//         client_thread.detach(); 
//     }

//     std::cout << "Tracker main thread shut down successfully.\n";
//     return 0;
// }

// void admin_console(int server_sock, TrackerSync& tracker_sync) {
//     std::string command;
//     while (std::getline(std::cin, command)) {
//         if (command == "quit") {
//             std::cout << "[Admin] Initiating graceful shutdown. Notifying clients...\n";
//             is_running.store(false);

//             // 1. Force all active clients' recv() calls to unblock by shutting down their read/write streams
//             {
//                 std::lock_guard<std::mutex> lock(clients_mutex);
//                 for (int client_sock : active_clients) {
//                     shutdown(client_sock, SHUT_RDWR);
//                 }
//             }

//             // 2. Wait for all client threads to finish cleanup and log out
//             while (true) {
//                 {
//                     std::lock_guard<std::mutex> lock(clients_mutex);
//                     if (active_clients.empty()) break;
//                 }
//                 std::this_thread::sleep_for(std::chrono::milliseconds(50));
//             }

//             std::cout << "[Admin] All clients cleaned up. Terminating sync...\n";
            
//             // 3. Stop inter-tracker sync
//             tracker_sync.stop();
            
//             // 4. Force accept() to unblock in main thread
//             close(server_sock); 
//             break;
//         }
//     }
// }

// void handle_client(int client_sock, UserManager& user_manager, GroupManager& group_manager, 
//                    FileManager& file_manager, TrackerSync& tracker_sync) {
//     std::cout << "Client session started on socket " << client_sock << "\n";
    
//     CommandType cmd;
//     std::string payload;
//     std::string current_user = ""; 

//     while (true) {
//         if (!recv_command(client_sock, cmd, payload)) {
//             if (!current_user.empty()) {
//                 user_manager.logout(current_user);
//                 tracker_sync.send_sync_message(format_sync_logout(current_user));
//             }
//             std::cout << "Client on socket " << client_sock << " disconnected.\n";
//             break;
//         }

//         std::vector<std::string> args = deserialize_args(payload);
//         std::string response_msg;

//         bool needs_auth = (cmd >= CMD_CREATE_GROUP && cmd != CMD_SYNC);
//         if (needs_auth && current_user.empty()) {
//             response_msg = "Error: You must be logged in to perform this action";
//         } 
//         else {
//             switch (cmd) {
//                 // --- INCOMING SYNC RECEPTION ---
//                 case CMD_SYNC: {
//                     if (args.empty()) break;
//                     std::string action = args[0];
                    
//                     if (action == SyncAction::USER && args.size() == 3) {
//                         user_manager.sync_user(args[1], args[2]);
//                     } else if (action == SyncAction::LOGIN && args.size() == 4) {
//                         user_manager.sync_login(args[1], args[2], std::stoi(args[3]));
//                     } else if (action == SyncAction::LOGOUT && args.size() == 2) {
//                         user_manager.sync_logout(args[1]);
//                     } else if (action == SyncAction::GROUP_CREATE && args.size() == 3) {
//                         group_manager.sync_create_group(args[1], args[2]);
//                     } else if (action == SyncAction::GROUP_JOIN && args.size() == 3) {
//                         group_manager.sync_join_group(args[1], args[2]);
//                     } else if (action == SyncAction::GROUP_ACCEPT && args.size() == 3) {
//                         group_manager.sync_accept_request(args[1], args[2]);
//                     } else if (action == SyncAction::GROUP_LEAVE && args.size() == 3) {
//                         group_manager.sync_leave_group(args[1], args[2]);
//                     } else if (action == SyncAction::FILE_UPLOAD && args.size() == 6) {
//                         file_manager.sync_upload_file(args[1], args[2], std::stoull(args[3]), args[4], args[5]);
//                     } else if (action == SyncAction::STOP_SHARE && args.size() == 4) {
//                         file_manager.sync_stop_share(args[1], args[2], args[3]);
//                     }
                    
//                     response_msg = "SYNC_ACK";
//                     break;
//                 }

//                 case CMD_CREATE_USER:
//                     if (args.size() == 2) {
//                         response_msg = user_manager.create_user(args[0], args[1]);
//                         if (response_msg.find("Error") == std::string::npos) {
//                             tracker_sync.send_sync_message(format_sync_user(args[0], args[1]));
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;
                
//                 case CMD_LOGIN:
//                     if (!current_user.empty()) {
//                         response_msg = "Error: You are already logged in as " + current_user + ". Please logout first.";
//                     } else if (args.size() == 3) { 
//                         try {
//                             int peer_port = std::stoi(args[2]);
//                             struct sockaddr_in peer_addr{};
//                             socklen_t peer_len = sizeof(peer_addr);
//                             getpeername(client_sock, (struct sockaddr*)&peer_addr, &peer_len);
//                             char client_ip[INET_ADDRSTRLEN];
//                             inet_ntop(AF_INET, &peer_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

//                             response_msg = user_manager.login(args[0], args[1], std::string(client_ip), peer_port);
//                             if (response_msg == "Login successful") {
//                                 current_user = args[0];
//                                 tracker_sync.send_sync_message(format_sync_login(args[0], std::string(client_ip), peer_port));
//                             }
//                         } catch (...) {
//                             response_msg = "Error: Invalid port format";
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_LOGOUT:
//                     if (current_user.empty()) response_msg = "Error: You are not logged in";
//                     else {
//                         response_msg = user_manager.logout(current_user);
//                         if (response_msg.find("Error") == std::string::npos) {
//                             tracker_sync.send_sync_message(format_sync_logout(current_user));
//                             current_user = ""; 
//                         }
//                     }
//                     break;

//                 case CMD_CREATE_GROUP:
//                     if (args.size() == 1) {
//                         response_msg = group_manager.create_group(args[0], current_user);
//                         if (response_msg.find("Error") == std::string::npos) {
//                             tracker_sync.send_sync_message(format_sync_group_create(args[0], current_user));
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_JOIN_GROUP:
//                     if (args.size() == 1) {
//                         response_msg = group_manager.join_group(args[0], current_user);
//                         if (response_msg.find("Error") == std::string::npos) {
//                             tracker_sync.send_sync_message(format_sync_group_join(args[0], current_user));
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_LEAVE_GROUP:
//                     if (args.size() == 1) {
//                         response_msg = group_manager.leave_group(args[0], current_user);
//                         if (response_msg.find("Error") == std::string::npos) {
//                             tracker_sync.send_sync_message(format_sync_group_leave(args[0], current_user));
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_LIST_GROUPS:
//                     if (args.empty()) response_msg = group_manager.list_groups();
//                     else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_LIST_REQUESTS:
//                     if (args.size() == 1) response_msg = group_manager.list_requests(args[0], current_user);
//                     else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_ACCEPT_REQUEST:
//                     if (args.size() == 2) {
//                         response_msg = group_manager.accept_request(args[0], args[1], current_user);
//                         if (response_msg.find("Error") == std::string::npos) {
//                             tracker_sync.send_sync_message(format_sync_group_accept(args[0], args[1]));
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_UPLOAD_FILE:
//                     if (args.size() == 4) {
//                         if (!group_manager.is_member(args[0], current_user)) {
//                             response_msg = "Error: You are not a member of group '" + args[0] + "'";
//                         } else {
//                             try {
//                                 size_t file_size = std::stoull(args[2]);
//                                 response_msg = file_manager.upload_file(args[0], args[1], file_size, args[3], current_user);
//                                 if (response_msg.find("Error") == std::string::npos) {
//                                     tracker_sync.send_sync_message(format_sync_file_upload(args[0], args[1], file_size, args[3], current_user));
//                                 }
//                             } catch (...) {
//                                 response_msg = "Error: Invalid file size format.";
//                             }
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_LIST_FILES:
//                     if (args.size() == 1) {
//                         if (!group_manager.is_member(args[0], current_user)) {
//                             response_msg = "Error: You are not a member of group '" + args[0] + "'";
//                         } else {
//                             response_msg = file_manager.list_files(args[0]);
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_DOWNLOAD_FILE:
//                     if (args.size() == 2) {
//                         if (!group_manager.is_member(args[0], current_user)) {
//                             response_msg = "Error: You are not a member of group '" + args[0] + "'";
//                         } else {
//                             response_msg = file_manager.download_file(args[0], args[1], user_manager);
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 case CMD_STOP_SHARE:
//                     if (args.size() == 2) {
//                         if (!group_manager.is_member(args[0], current_user)) {
//                             response_msg = "Error: You are not a member of group '" + args[0] + "'";
//                         } else {
//                             response_msg = file_manager.stop_share(args[0], args[1], current_user);
//                             if (response_msg.find("Error") == std::string::npos) {
//                                 tracker_sync.send_sync_message(format_sync_stop_share(args[0], args[1], current_user));
//                             }
//                         }
//                     } else response_msg = "Error: Invalid arguments.";
//                     break;

//                 default:
//                     response_msg = "Error: Unknown or unimplemented command";
//                     break;
//             }
//         }
//         if(cmd==CMD_SYNC){
//             continue;             //    <<<<<----- skip sending response when the message was a sync message.
//         }

//         if (!send_command(client_sock, CMD_RESPONSE, response_msg)) {
//             if (!current_user.empty()) {
//                 user_manager.logout(current_user);
//                 tracker_sync.send_sync_message(format_sync_logout(current_user));
//             }
//             break; // Breaks loop, proceeds to cleanup
//         }
//     }

//     // Thread is about to exit. Remove from set and close FD.
//     close(client_sock);
//     {
//         std::lock_guard<std::mutex> lock(clients_mutex);
//         active_clients.erase(client_sock);
//     }
// }