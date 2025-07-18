# 💬 Chat Server

A **feature-rich, multi-threaded, terminal-based chat application** built in C++ using Winsock for networking. Implements a client-server architecture supporting multiple users, public/private rooms, direct messaging, and admin management.

---

## 🚀 Key Features

- **User Authentication**  
    Secure login/signup with user data persisted in a local CSV file.

- **Multi-Room Chat**  
    Create, join, list, and leave chat rooms. Return to the main lobby anytime.

- **Real-Time Communication**  
    - Chat in the main lobby with all users  
    - Private room-specific chat  
    - Direct one-to-one messaging

- **Admin Capabilities**  
    - Role-based permissions (User/Admin)  
    - View all users across rooms (`/whoall`)  
    - Remove users from rooms (`/kick`)  
    - Permanently delete rooms (`/deleteroom`)

- **Dynamic UI**  
    Responsive terminal interface using ANSI escape codes for colored text, dynamic prompts, and clean UI updates.

---

## 🛠️ Technical Stack & Concepts

| Component         | Details                                                                                  |
|-------------------|-----------------------------------------------------------------------------------------|
| **Language**      | C++17 (`<thread>`, `<mutex>`, `<vector>`, smart stream manipulation)                    |
| **Networking**    | TCP/IP Sockets (Winsock2 library, Windows only)                                         |
| **Architecture**  | Client-Server (one-thread-per-client concurrency model)                                 |
| **Synchronization** | `std::mutex` and `std::lock_guard` for thread-safe access to shared data              |
| **Protocol**      | Custom, line-based ASCII protocol (`\n` as message delimiter)                           |
| **Persistence**   | User credentials, admin status, and nicknames saved to `users.csv`                      |
| **UI**            | Terminal UI managed with ANSI escape codes for color, cursor movement, and line clearing|

---

## ⚡ Getting Started

### Prerequisites

- Windows environment
- C++17 compiler (e.g., MinGW g++ 11+)

### Compilation

```bash
# Compile the server
g++ server.cpp -o server.exe -std=c++17 -lws2_32 -static

# Compile the client
g++ client.cpp -o client.exe -std=c++17 -lws2_32 -static
```

### Execution

Start the server (in one terminal):

```bash
./server.exe
```

Start clients (in separate terminals):

```bash
./client.exe
```

---

## 🧪 Feature Testing Scenario

### 1. User Authentication

- **Client 1:** Signup  
- **Client 2:** Login

### 2. Chat & Commands

<details>
    <summary>User Commands</summary>

    ```
    /help                     -> Show this help menu
    /who                      -> Show users in your current room
    /list                     -> List all active chat rooms
    /leave                    -> Leave the current room to the Lobby
    /create <roomname>        -> Create a new chat room
    /join <roomname>          -> Join an existing chat room
    /msg <username> <message> -> Send a private message
    /exit                     -> Quit the chat
    ```
</details>

<details>
    <summary>Admin Commands</summary>

    ```
    /whoall                   -> List all online users
    /kick <username>          -> Kick user to the Lobby
    /deleteroom <roomname>    -> Delete a chat room
    ```
</details>

### 3. Room Management

- **PlayerOne:** `/create gaming`
- **TesterTwo:** `/list` (should show `gaming`)
- **Both:** `/join gaming`
- Chat in `gaming` room; Lobby users can't see messages
- `/leave` to return to Lobby

### 4. Private & Admin Messaging

- **PlayerOne:** `/msg <username> This is a private test`
- **Admin:** `/whoall` (lists all users/rooms)
- **PlayerOne:** `/whoall` (permission denied)

### 5. Admin Actions

- **TesterTwo** joins `gaming`
- **Admin:** `/kick ts` (TesterTwo returns to Lobby)
- **Admin:** `/deleteroom gaming` (room deleted, notifications sent)

---

## 🚧 Drawbacks & Scope for Improvement

While this project achieves its core goals and demonstrates key concepts, it was designed as a learning exercise. A production-ready chat server would benefit from enhancements in several areas:

### 1. Scalability

- **Current Limitation:**  
    The server uses a one-thread-per-client model, which is simple but does not scale efficiently. High thread counts can degrade performance due to context-switching overhead.

- **Potential Improvement:**  
    Adopt an asynchronous, event-driven I/O model (e.g., IOCP on Windows, epoll on Linux, or Boost.Asio for cross-platform support) to handle thousands of concurrent connections with fewer threads.

### 2. Security and Data Persistence

- **Current Limitation:**  
    User passwords are stored in plain text within `users.csv`, and all communications are sent unencrypted over the network.

- **Potential Improvement:**  
    - Store passwords as securely hashed and salted values (e.g., bcrypt, Argon2).  
    - Encrypt all network traffic using TLS/SSL (OpenSSL or similar), protecting user privacy and credentials.

### 3. History / Data Storage

- **Current Limitation:**  
    Chat messages are ephemeral and are not saved anywhere. When the server restarts, all conversations are lost. Users joining a room have no context of the previous discussion.

- **Potential Improvement:**  
    Implement a chat logging system. A simple solution would be to append messages to a text file for each room (e.g., `logs/room_name.txt`). A more advanced implementation would store messages in a database, allowing for features like retrieving the last N messages when a user joins a room.

---

> **Enjoy chatting!** 🎉

