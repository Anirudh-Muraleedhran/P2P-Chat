#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "sqlite3.h"

using namespace std;

#pragma comment(lib,"Ws2_32.lib")

#define PORT 5000
#define BUFFER_SIZE 1024

sqlite3* db;

mutex db_mutex;
mutex socket_mutex;
mutex name_mutex;

map<string, SOCKET> client_names;
map<string, int> client_user_id;

// ---------------- DATABASE INIT ----------------
void init_database()
{
    sqlite3_open("chat_history.db", &db);

    const char* user_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL);";

    const char* message_sql =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender_id INTEGER NOT NULL,"
        "receiver_id INTEGER,"               // NULL = broadcast
        "content TEXT NOT NULL,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(sender_id) REFERENCES users(id),"
        "FOREIGN KEY(receiver_id) REFERENCES users(id));";

    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, user_sql, nullptr, nullptr, nullptr);
    sqlite3_exec(db, message_sql, nullptr, nullptr, nullptr);
}

// ---------------- USER MANAGEMENT ----------------
int get_or_create_user(const string& username)
{
    lock_guard<mutex> lock(db_mutex);

    sqlite3_stmt* stmt = nullptr;
    int user_id = -1;

    sqlite3_prepare_v2(
        db,
        "SELECT id FROM users WHERE username = ?",
        -1, &stmt, nullptr
    );

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        user_id = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);

    if (user_id == -1)
    {
        sqlite3_prepare_v2(
            db,
            "INSERT INTO users(username) VALUES(?)",
            -1, &stmt, nullptr
        );

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        user_id = (int)sqlite3_last_insert_rowid(db);
    }

    return user_id;
}

// ---------------- MESSAGE STORAGE ----------------
void save_message(int sender_id, int receiver_id, const string& content)
{
    if (content.empty() || content.length() > 500)
        return;

    lock_guard<mutex> lock(db_mutex);

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "INSERT INTO messages(sender_id, receiver_id, content) "
        "VALUES (?, ?, ?)";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        cerr << "[DB ERROR] Prepare failed: "
             << sqlite3_errmsg(db) << endl;
        return;
    }

    sqlite3_bind_int(stmt, 1, sender_id);

    // NULL receiver_id = broadcast
    if (receiver_id == 0)
        sqlite3_bind_null(stmt, 2);
    else
        sqlite3_bind_int(stmt, 2, receiver_id);

    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        cerr << "[DB ERROR] Insert failed: "
             << sqlite3_errmsg(db) << endl;
    }

    sqlite3_finalize(stmt);
}

// ---------------- LOAD HISTORY ----------------
void load_history(SOCKET sock, int user_id)
{
    lock_guard<mutex> lock(db_mutex);

    sqlite3_stmt* stmt = nullptr;

    string sql =
        "SELECT username, content FROM ( "
        "SELECT u.username AS username, m.content, m.id "
        "FROM messages m "
        "JOIN users u ON u.id = m.sender_id "
        "WHERE m.sender_id = ? "
        "   OR m.receiver_id = ? "
        "   OR m.receiver_id IS NULL "
        "ORDER BY m.id DESC "
        "LIMIT 30 "
        ") "
        "ORDER BY id ASC;";

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        cerr << "[DB ERROR] " << sqlite3_errmsg(db) << endl;
        return;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, user_id);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        string msg =
            "[HISTORY] " +
            string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) +
            ": " +
            string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))) +
            "\n";

        send(sock, msg.c_str(), msg.length(), 0);
    }

    sqlite3_finalize(stmt);
}

// ---------------- BROADCAST ----------------
void broadcast(const string& msg, SOCKET exclude = INVALID_SOCKET)
{
    lock_guard<mutex> lock(name_mutex);

    for (auto it = client_names.begin(); it != client_names.end(); )
    {
        SOCKET sock = it->second;

        if (sock != exclude)
        {
            if (send(sock, msg.c_str(), msg.length(), 0) == SOCKET_ERROR)
            {
                closesocket(sock);
                it = client_names.erase(it);
                continue;
            }
        }
        ++it;
    }
}

// ---------------- CLIENT HANDLER ----------------
void handle_client(SOCKET sock)
{
    char buffer[BUFFER_SIZE];

    int n = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (n <= 0)
    {
        closesocket(sock);
        return;
    }

    buffer[n] = '\0';
    string name(buffer);
    name.erase(name.find_last_not_of("\n\r") + 1);

    {
        lock_guard<mutex> lock(name_mutex);
        if (client_names.count(name))
        {
            string err = "[SERVER] Username already taken\n";
            send(sock, err.c_str(), err.length(), 0);
            closesocket(sock);
            return;
        }
    }

    int user_id = get_or_create_user(name);

    {
        lock_guard<mutex> lock(name_mutex);
        client_names[name] = sock;
        client_user_id[name] = user_id;
    }

    load_history(sock, user_id);
    broadcast(name + " joined the chat\n", sock);

    while ((n = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0)
    {
        buffer[n] = '\0';
        string msg(buffer);
        msg.erase(msg.find_last_not_of("\n\r") + 1);

        save_message(user_id, 0, msg);
        broadcast("[" + name + "]: " + msg + "\n", sock);
    }

    {
        lock_guard<mutex> lock(name_mutex);
        client_names.erase(name);
        client_user_id.erase(name);
    }

    broadcast(name + " left the chat\n");
    closesocket(sock);
}

// ---------------- MAIN ----------------
int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    init_database();

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(listen_socket, (SOCKADDR*)&addr, sizeof(addr));
    listen(listen_socket, SOMAXCONN);

    cout << "Server listening on port " << PORT << endl;

    while (true)
    {
        sockaddr_in client_addr;
        int len = sizeof(client_addr);

        SOCKET client = accept(listen_socket, (SOCKADDR*)&client_addr, &len);
        if (client != INVALID_SOCKET)
            thread(handle_client, client).detach();
    }

    sqlite3_close(db);
    WSACleanup();
    return 0;
}
