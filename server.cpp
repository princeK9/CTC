#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_BUFFER_SIZE 4096

struct User {
    std::string username;
    std::string password;
    bool isAdmin;
    std::string nickname;
};

struct ClientInfo {
    SOCKET socket;
    std::string username;
    std::string nickname;
    int id;
    std::string current_room;
    bool isAdmin;
};

std::vector<ClientInfo> clients;
std::vector<std::string> rooms;
std::mutex clients_mutex;
std::mutex rooms_mutex;
std::mutex user_file_mutex;
int next_client_id = 1;

void send_to_client(SOCKET sock, const std::string& message) {
    std::string formatted_msg = message + "\n";
    send(sock, formatted_msg.c_str(), (int)formatted_msg.length(), 0);
}

// An "unlocked" version for use when the mutex is already held
void broadcast_to_room_unlocked(const std::string& room_name, const std::string& message) {
    for (const auto& client : clients) {
        if (client.current_room == room_name) {
            send_to_client(client.socket, message);
        }
    }
}

void broadcast_to_room(const std::string& room_name, const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    broadcast_to_room_unlocked(room_name, message);
}

std::vector<User> load_users() {
    std::lock_guard<std::mutex> lock(user_file_mutex);
    std::vector<User> users;
    std::ifstream file("users.csv");
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string username, password, isAdminStr, nickname;
        if (std::getline(ss, username, ',') && std::getline(ss, password, ',') && std::getline(ss, isAdminStr, ',') && std::getline(ss, nickname)) {
            users.push_back({username, password, (isAdminStr == "true"), nickname});
        }
    }
    return users;
}

bool save_user(const User& user) {
    auto all_users = load_users();
    for (const auto& u : all_users) {
        if (u.username == user.username) {
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(user_file_mutex);
    std::ofstream file("users.csv", std::ios::app);
    file << user.username << "," << user.password << "," << (user.isAdmin ? "true" : "false") << "," << user.nickname << "\n";
    return true;
}

void handle_client(SOCKET client_socket, int id) {
    char buffer[MAX_BUFFER_SIZE];
    std::string current_username;
    std::string current_nickname;
    bool is_admin = false;
    bool authenticated = false;

    while (!authenticated) {
        int bytes_received = recv(client_socket, buffer, MAX_BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) { closesocket(client_socket); return; }
        buffer[bytes_received] = '\0';
        std::stringstream ss(buffer);
        std::string command, username, password, nickname;
        ss >> command >> username >> password >> nickname;

        if (command == "LOGIN") {
            auto users = load_users();
            bool found = false;
            for (const auto& user : users) {
                if (user.username == username && user.password == password) {
                    send_to_client(client_socket, "AUTH_SUCCESS " + std::string(user.isAdmin ? "true" : "false") + " " + user.nickname);
                    current_username = user.username;
                    current_nickname = user.nickname;
                    is_admin = user.isAdmin;
                    authenticated = true;
                    found = true;
                    break;
                }
            }
            if (!found) send_to_client(client_socket, "AUTH_FAIL Invalid credentials");
        } else if (command == "SIGNUP") {
            if (nickname == "N/A" || nickname.empty()){
                send_to_client(client_socket, "AUTH_FAIL Nickname cannot be empty.");
            }
            else if (save_user({username, password, false, nickname})) {
                send_to_client(client_socket, "AUTH_SUCCESS false " + nickname);
                current_username = username;
                current_nickname = nickname;
                is_admin = false;
                authenticated = true;
            } else {
                send_to_client(client_socket, "AUTH_FAIL User already exists");
            }
        } else {
            send_to_client(client_socket, "AUTH_FAIL Invalid command");
        }
    }

    std::string initial_room = "Lobby";
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back({client_socket, current_username, current_nickname, id, initial_room, is_admin});
    }
    std::string welcome_message = "[" + initial_room + "] " + current_nickname + " has joined!";
    std::cout << welcome_message << std::endl;
    broadcast_to_room(initial_room, "SYS_MSG " + welcome_message);

    while (true) {
        int bytes_received = recv(client_socket, buffer, MAX_BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) break;
        
        buffer[bytes_received] = '\0';
        std::string message(buffer);
        std::stringstream msg_stream(message);
        std::string command;
        msg_stream >> command;
        
        std::string user_current_room;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            auto it = std::find_if(clients.begin(), clients.end(), [id](const ClientInfo& c){ return c.id == id; });
            if(it != clients.end()) user_current_room = it->current_room;
        }

        if (command == "/exit") {
            break;
        } else if (command == "/who") {
            std::string user_list_msg = "CMD_RESP --- Users in [" + user_current_room + "] ---";
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (const auto& client : clients) {
                if (client.current_room == user_current_room) {
                    user_list_msg += "| - " + client.nickname;
                }
            }
            send_to_client(client_socket, user_list_msg);
        } else if (command == "/whoall") {
            if (!is_admin) {
                send_to_client(client_socket, "CMD_RESP [Error] You do not have permission to use this command.");
            } else {
                std::string user_list_msg = "CMD_RESP --- All Online Users ---";
                std::lock_guard<std::mutex> lock(clients_mutex);
                for (const auto& client : clients) {
                    user_list_msg += "| - " + client.nickname + " (" + client.username + ") in [" + client.current_room + "]";
                }
                send_to_client(client_socket, user_list_msg);
            }
        } else if (command == "/list") {
            std::string room_list_msg = "CMD_RESP --- Active Rooms ---";
            std::lock_guard<std::mutex> lock(rooms_mutex);
            if (rooms.empty()) {
                room_list_msg += "|[No rooms available yet]";
            } else {
                for (const auto& room : rooms) {
                    room_list_msg += "| - " + room;
                }
            }
            send_to_client(client_socket, room_list_msg);
        } else if (command == "/create") {
            std::string room_name;
            msg_stream >> room_name;
            if (room_name.empty() || room_name == "Lobby") {
                send_to_client(client_socket, "CMD_RESP [Error] Invalid room name.");
            } else {
                std::lock_guard<std::mutex> lock(rooms_mutex);
                if (std::find(rooms.begin(), rooms.end(), room_name) != rooms.end()) {
                    send_to_client(client_socket, "CMD_RESP [Error] Room '" + room_name + "' already exists.");
                } else {
                    rooms.push_back(room_name);
                    send_to_client(client_socket, "CMD_RESP Room '" + room_name + "' created successfully.");
                }
            }
        } else if (command == "/join") {
            std::string room_name;
            msg_stream >> room_name;
            bool room_exists;
            {
                std::lock_guard<std::mutex> lock(rooms_mutex);
                room_exists = (std::find(rooms.begin(), rooms.end(), room_name) != rooms.end());
            }
             if (!room_exists && room_name != "Lobby") {
                send_to_client(client_socket, "CMD_RESP [Error] Room '" + room_name + "' does not exist.");
            } else if (user_current_room == room_name) {
                send_to_client(client_socket, "CMD_RESP [Error] You are already in that room.");
            } else {
                broadcast_to_room(user_current_room, "SYS_MSG [" + user_current_room + "] " + current_nickname + " has left.");
                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    for (auto& client : clients) { if (client.id == id) { client.current_room = room_name; break; } }
                }
                send_to_client(client_socket, "JOIN_SUCCESS " + room_name);
                broadcast_to_room(room_name, "SYS_MSG [" + room_name + "] " + current_nickname + " has joined!");
            }
        } else if (command == "/leave") {
            if (user_current_room == "Lobby") {
                send_to_client(client_socket, "CMD_RESP [Error] You are already in the Lobby.");
            } else {
                std::string old_room = user_current_room;
                std::string new_room = "Lobby";
                broadcast_to_room(old_room, "SYS_MSG [" + old_room + "] " + current_nickname + " has left.");
                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    for (auto& client : clients) { if (client.id == id) { client.current_room = new_room; break; } }
                }
                send_to_client(client_socket, "JOIN_SUCCESS " + new_room);
                broadcast_to_room(new_room, "SYS_MSG [" + new_room + "] " + current_nickname + " has joined!");
            }
        } else if (command == "/msg") {
            std::string target_username;
            std::string private_message;
            msg_stream >> target_username;
            std::getline(msg_stream >> std::ws, private_message);

            if (target_username.empty() || private_message.empty()) {
                send_to_client(client_socket, "CMD_RESP [Error] Usage: /msg <username> <message>");
            } else if (target_username == current_username) {
                send_to_client(client_socket, "CMD_RESP [Error] You cannot send a private message to yourself.");
            } else {
                SOCKET target_socket = INVALID_SOCKET;
                std::string target_nickname;
                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    for(const auto& client : clients) {
                        if (client.username == target_username) {
                            target_socket = client.socket;
                            target_nickname = client.nickname;
                            break;
                        }
                    }
                }
                if (target_socket == INVALID_SOCKET) {
                    send_to_client(client_socket, "CMD_RESP [Error] User '" + target_username + "' not found or is not online.");
                } else {
                    std::string formatted_to_sender = "P_MSG (to " + target_nickname + "): " + private_message;
                    std::string formatted_to_receiver = "P_MSG (from " + current_nickname + "): " + private_message;
                    send_to_client(target_socket, formatted_to_receiver);
                    send_to_client(client_socket, formatted_to_sender);
                }
            }
        } else if (command == "/kick") {
             if (!is_admin) {
                send_to_client(client_socket, "CMD_RESP [Error] You do not have permission to use this command.");
            } else {
                std::string target_username;
                msg_stream >> target_username;
                std::string kicked_from_room, target_nickname;
                SOCKET target_socket = INVALID_SOCKET;
                bool success = false;
                
                std::lock_guard<std::mutex> lock(clients_mutex);
                auto it = std::find_if(clients.begin(), clients.end(), [&](const ClientInfo& c){ return c.username == target_username; });
                if (it == clients.end()) {
                    send_to_client(client_socket, "CMD_RESP [Error] User '" + target_username + "' not found.");
                } else if (it->isAdmin) {
                    send_to_client(client_socket, "CMD_RESP [Error] You cannot kick another admin.");
                } else if (it->current_room == "Lobby") {
                    send_to_client(client_socket, "CMD_RESP [Info] User '" + target_username + "' is already in the Lobby.");
                }
                else {
                    kicked_from_room = it->current_room;
                    target_nickname = it->nickname;
                    target_socket = it->socket;
                    it->current_room = "Lobby";
                    success = true;
                }

                if(success) {
                    send_to_client(target_socket, "SYS_MSG You have been kicked back to the Lobby by an admin.");
                    send_to_client(target_socket, "JOIN_SUCCESS Lobby");
                    broadcast_to_room_unlocked(kicked_from_room, "SYS_MSG [" + kicked_from_room + "] " + target_nickname + " was kicked by an admin.");
                    send_to_client(client_socket, "CMD_RESP User '" + target_nickname + "' has been kicked to the Lobby.");
                }
            }
        } else if (command == "/deleteroom") {
            if (!is_admin) {
                send_to_client(client_socket, "CMD_RESP [Error] You do not have permission to use this command.");
            } else {
                std::string room_to_delete;
                msg_stream >> room_to_delete;
                if (room_to_delete == "Lobby") {
                    send_to_client(client_socket, "CMD_RESP [Error] You cannot delete the Lobby.");
                } else {
                    bool room_found_and_deleted = false;
                    {
                        std::lock_guard<std::mutex> lock(rooms_mutex);
                        auto room_it = std::find(rooms.begin(), rooms.end(), room_to_delete);
                        if (room_it != rooms.end()) {
                            rooms.erase(room_it);
                            room_found_and_deleted = true;
                        }
                    }

                    if (!room_found_and_deleted) {
                        send_to_client(client_socket, "CMD_RESP [Error] Room '" + room_to_delete + "' does not exist.");
                    } else {
                        std::string admin_msg = "SYS_MSG [SYSTEM] Room '" + room_to_delete + "' was deleted by " + current_nickname + ".";
                        std::string user_msg = "SYS_MSG [SYSTEM] Room '" + room_to_delete + "' has been deleted.";
                        
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        for (auto& client : clients) {
                            if (client.current_room == room_to_delete) {
                                client.current_room = "Lobby";
                                send_to_client(client.socket, "SYS_MSG Room '" + room_to_delete + "' has been deleted. You are now in the Lobby.");
                                send_to_client(client.socket, "JOIN_SUCCESS Lobby");
                            }
                        }
                        send_to_client(client_socket, "CMD_RESP Room '" + room_to_delete + "' has been deleted.");
                        
                        // Send differentiated messages to the Lobby
                        for (const auto& client : clients) {
                            if(client.current_room == "Lobby") {
                                if(client.isAdmin) {
                                    send_to_client(client.socket, admin_msg);
                                } else {
                                    send_to_client(client.socket, user_msg);
                                }
                            }
                        }
                    }
                }
            }
        }
        else {
            std::string msg_body = message;
            std::string formatted_message = "MSG " + std::to_string(id) + " " + current_nickname + " [" + user_current_room + "] " + msg_body;
            broadcast_to_room(user_current_room, formatted_message);
        }
    }
    
    std::string final_room;
    std::string final_nickname;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(std::remove_if(clients.begin(), clients.end(), [&](const ClientInfo& c) {
            if (c.id == id) { 
                final_room = c.current_room; 
                final_nickname = c.nickname;
                return true; 
            } 
            return false; 
        }), clients.end());
    }
    std::string farewell_message = "[" + final_room + "] " + final_nickname + " has left the chat.";
    std::cout << farewell_message << std::endl;
    broadcast_to_room(final_room, "SYS_MSG " + farewell_message);
    closesocket(client_socket);
}

int main() {
    WSADATA wsaData; if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0); if (server_socket == INVALID_SOCKET) { WSACleanup(); return 1; }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(10000);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) { closesocket(server_socket); WSACleanup(); return 1; }
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) { closesocket(server_socket); WSACleanup(); return 1; }
    std::ifstream f("users.csv");
    if (!f.good() || f.peek() == std::ifstream::traits_type::eof()) { save_user({"admin", "admin", true, "Admin"}); std::cout << "[INFO] users.csv created with default admin user." << std::endl; }
    f.close();
    std::cout << "[SERVER] Started and listening on port 10000." << std::endl;
    while (true) {
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) continue;
        std::thread(handle_client, client_socket, next_client_id++).detach();
    }
    closesocket(server_socket);
    WSACleanup();
    return 0;
}