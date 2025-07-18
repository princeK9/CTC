#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

#define MAX_BUFFER_SIZE 4096
#define NUM_COLORS 6

SOCKET client_socket;
bool exit_flag = false;
std::string username;
std::string nickname;
std::string current_room = "Lobby";
bool is_client_admin = false;
std::mutex console_mutex;

std::string def_col = "\033[0m";
std::string colors[] = { "\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m", "\033[95m" }; 

std::string get_color(int code) {
    return colors[code % NUM_COLORS];
}

void enable_virtual_terminal_processing() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, mode)) return;
}

void clear_current_line() {
    std::cout << "\r\033[K" << std::flush;
}

void display_prompt() {
    std::cout << get_color(1) << nickname << " [" << current_room << "] : " << def_col << std::flush;
}

void print_help() {
    std::lock_guard<std::mutex> lock(console_mutex);
    clear_current_line();
    std::cout << get_color(2) << "--- User Commands ---" << std::endl;
    std::cout << " /help                     -> Show this help menu" << std::endl;
    std::cout << " /who                      -> Show users in your current room" << std::endl;
    std::cout << " /list                     -> List all active chat rooms" << std::endl;
    std::cout << " /leave                    -> Leave the current room to the Lobby" << std::endl;
    std::cout << " /create <roomname>        -> Create a new chat room" << std::endl;
    std::cout << " /join <roomname>          -> Join an existing chat room" << std::endl;
    std::cout << " /msg <username> <message> -> Send a private message" << std::endl;
    std::cout << " /exit                     -> Quit the chat" << std::endl;
    if (is_client_admin) {
        std::cout << "--- Admin Commands ---" << std::endl;
        std::cout << " /whoall                   -> List all online users" << std::endl;
        std::cout << " /kick <username>          -> Kick user to the Lobby" << std::endl;
        std::cout << " /deleteroom <roomname>    -> Delete a chat room" << std::endl;
    }
    std::cout << def_col;
    display_prompt();
}

void process_message(const std::string& received_str) {
    std::lock_guard<std::mutex> lock(console_mutex);
    clear_current_line();

    std::stringstream ss(received_str);
    std::string type;
    ss >> type;
    
    std::string body;
    std::getline(ss >> std::ws, body);

    if (type == "MSG") {
        std::stringstream msg_ss(body);
        int color_code;
        std::string sender_nick, room_tag, msg_body;
        msg_ss >> color_code >> sender_nick >> room_tag;
        std::getline(msg_ss, msg_body);
        
        if (sender_nick == nickname) {
             std::cout << get_color(1) << sender_nick << " " << room_tag << ":" << def_col << msg_body << std::endl;
        } else {
             std::cout << get_color(color_code) << sender_nick << " " << room_tag << ":" << def_col << msg_body << std::endl;
        }

    } else if (type == "SYS_MSG") {
        std::cout << get_color(5) << body << def_col << std::endl;
    } else if (type == "P_MSG") {
        std::cout << get_color(3) << body << def_col << std::endl;
    } else if (type == "CMD_RESP") {
        std::replace(body.begin(), body.end(), '|', '\n');
        std::cout << get_color(2) << body << std::endl;
    } else if (type == "JOIN_SUCCESS") {
        current_room = body;
        std::cout << get_color(2) << "Successfully moved to [" << current_room << "]." << std::endl;
    }
    display_prompt();
}

void recv_message() {
    char buffer[MAX_BUFFER_SIZE];
    std::string receive_buffer;
    while (!exit_flag) {
        int bytes_received = recv(client_socket, buffer, MAX_BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            console_mutex.lock();
            clear_current_line();
            std::cout << "\nServer connection lost." << std::endl;
            console_mutex.unlock();
            exit_flag = true;
            break;
        }
        buffer[bytes_received] = '\0';
        receive_buffer += buffer;

        size_t pos;
        while ((pos = receive_buffer.find('\n')) != std::string::npos) {
            std::string message = receive_buffer.substr(0, pos);
            receive_buffer.erase(0, pos + 1);
            if (!message.empty()) {
                process_message(message);
            }
        }
    }
}

void send_message() {
    std::string line;
    while (!exit_flag) {
        std::getline(std::cin, line);
        if (exit_flag) break;

        {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "\033[1A\r\033[K" << std::flush;
        }

        if (line.empty()) {
            std::lock_guard<std::mutex> lock(console_mutex);
            display_prompt();
            continue;
        };

        if (line == "/help") {
            print_help();
            continue;
        }

        if (line == "/exit") {
            exit_flag = true;
        }
        
        send(client_socket, line.c_str(), (int)line.length(), 0);
        if (line == "/exit") break;
    }
}

int main() {
    enable_virtual_terminal_processing();
    WSADATA wsaData; if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
    client_socket = socket(AF_INET, SOCK_STREAM, 0); if (client_socket == INVALID_SOCKET) { WSACleanup(); return 1; }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(10000);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(client_socket); WSACleanup(); return 1;
    }

    bool authenticated = false;
    while (!authenticated && !exit_flag) {
        std::cout << "\033[1;36m" << "+------------------------------------------+" << std::endl;
        std::cout << "|        Welcome to the CTC Server!        |" << std::endl;
        std::cout << "+------------------------------------------+" << "\033[0m" << std::endl;
        std::cout << "\033[33m" << "1. Login" << std::endl;
        std::cout << "2. Signup" << "\033[0m" << std::endl;
        std::cout << "Enter choice: ";
        
        std::string choice;
        std::getline(std::cin, choice);
        if (choice == "1" || choice == "2") {
            std::cout << "Enter Username: ";
            std::string user; std::getline(std::cin, user);
            std::cout << "Enter Password: ";
            std::string pass; std::getline(std::cin, pass);
            std::string nick = "N/A";

            if (user.empty() || pass.empty()) {
                std::cout << "\033[31m" << "Username and password cannot be empty.\n\n" << "\033[0m";
                continue;
            }

            if(choice == "2") {
                std::cout << "Choose a Nickname: ";
                std::getline(std::cin, nick);
                if(nick.empty()) {
                    std::cout << "\033[31m" << "Nickname cannot be empty.\n\n" << "\033[0m";
                    continue;
                }
            }

            std::string request = (choice == "1" ? "LOGIN " : "SIGNUP ") + user + " " + pass + " " + nick;
            send(client_socket, request.c_str(), (int)request.length(), 0);

            char auth_buf[1024];
            int bytes_received = recv(client_socket, auth_buf, 1023, 0);
            if (bytes_received <= 0) {
                std::cout << "Server disconnected." << std::endl;
                exit_flag = true; break;
            }
            auth_buf[bytes_received] = '\0';
            std::string response(auth_buf);
            response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
            
            std::stringstream resp_ss(response);
            std::string status, admin_str;
            resp_ss >> status >> admin_str;

            if (status == "AUTH_SUCCESS") {
                authenticated = true;
                username = user;
                is_client_admin = (admin_str == "true");
                std::getline(resp_ss >> std::ws, nickname);
                std::cout << "\033[2J\033[1;1H";
            } else {
                std::string error_msg = response.substr(response.find(" ") + 1);
                std::cout << get_color(0) << "Error: " << error_msg << def_col << "\n\n";
            }
        } else {
            std::cout << "Invalid choice. Please enter 1 or 2.\n\n";
        }
    }

    if (authenticated) {
        std::cout << get_color(5) << "+" << std::string(50, '-') << "+" << std::endl;
        std::cout << "|    " << get_color(4) << "Welcome to the Chat Terminal, " << nickname << "! :)" << std::string(20 - nickname.length(), ' ') << get_color(5) << "|" << std::endl;
        std::cout << "|    " << get_color(3) << "You are in the lobby. Type /help for commands. " << get_color(5) << " |" << std::endl;
        std::cout << "+" << std::string(50, '-') << "+" << def_col << std::endl << std::endl;
        
        display_prompt();

        std::thread t_recv(recv_message);
        std::thread t_send(send_message);
        
        if (t_send.joinable()) t_send.join();
        if (t_recv.joinable()) t_recv.join();
    }
    closesocket(client_socket);
    WSACleanup();
    return 0;
}