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

//vector<SOCKET> client_sockets;
map<string, SOCKET> client_names;
map<string,int> client_user_id;


void init_database()
{
    sqlite3_open("chat_history.db", &db);
    const char* user_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL)";

    const char* message_sql = "CREATE TABLE IF NOT EXISTS messages("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "sender_id INTEGER,"
                              "receiver_id INTEGER,"
                              "content TEXT,"
                              "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                              "FOREIGN KEY(sender_id) REFERENCES users(id),"
                              "FOREIGN KEY(receiver_id) REFERENCES users(id));";

    sqlite3_exec(db,"PRAGMA foreign_keys = ON;",nullptr,nullptr,nullptr);
    sqlite3_exec(db, user_sql, nullptr, nullptr, nullptr);
    sqlite3_exec(db, message_sql, nullptr, nullptr, nullptr);
}

int get_or_create_user(const string& username)
{
    lock_guard<mutex> lock(db_mutex);

    sqlite3_stmt * stmt;
    int user_id = -1;

    //find if user exist
    sqlite3_prepare_v2(db,"SELECT id FROM users WHERE username = ?",-1,&stmt,nullptr);

    sqlite3_bind_text(stmt,1,username.c_str(),-1,SQLITE_TRANSIENT);

    if(sqlite3_step(stmt)==SQLITE_ROW)
    {
        user_id = sqlite3_column_int(stmt,0);
    }

    sqlite3_finalize(stmt);

    //if user not found then we must createa new user 
    if(user_id == -1)
    {
        sqlite3_prepare_v2(db,"INSERT INTO users(username) VALUES(?)",-1,&stmt,nullptr);
        sqlite3_bind_text(stmt,1,username.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        user_id = (int)sqlite3_last_insert_rowid(db);
    }
    return user_id;
}

void save_message(int sender_id,int receiver_id,const string& content)
{
    lock_guard<mutex> lock(db_mutex);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO messages(sender_id,receiver_id,content) VALUES(?,?,?)",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, sender_id);
    sqlite3_bind_int(stmt, 2, receiver_id);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void load_history(SOCKET sock,int user_id)
{
    lock_guard<mutex> lock(db_mutex);

    sqlite3_stmt* stmt;
    string sql =
    "SELECT * FROM ( "
    "SELECT u.username, m.content "
    "FROM messages m "
    "JOIN users u ON u.id = m.sender_id "
    "WHERE m.sender_id = ? "
    "   OR m.receiver_id = ? "
    "   OR m.receiver_id = 0 "
    "ORDER BY m.id DESC "
    "LIMIT 30 "
    ") "
    "ORDER BY id ASC;";




    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr)!=SQLITE_OK)
    {
        cerr<<"SQLite error : "<<sqlite3_errmsg(db)<<endl;
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

void broadcast(const string& msg, SOCKET exclude = INVALID_SOCKET)
{
    lock_guard<mutex> lock(name_mutex);
    for (auto& [_, sock] : client_names)
        if (sock != exclude)
            send(sock, msg.c_str(), msg.length(), 0);
}

void handle_client(SOCKET sock)
{
    char buffer[BUFFER_SIZE];

    //username
    int n = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (n <= 0)
    {
        closesocket(sock);
        return ;
    }

    buffer[n] = '\0';
    string name(buffer);
    name.erase(name.find_last_not_of("\n\r") + 1);

    //check id username already connected
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

    //Get or creating the id
    int user_id = get_or_create_user(name);

    //registering user
    {
        lock_guard<mutex> lock(name_mutex);
        client_names[name]= sock;
        client_user_id[name]= user_id;
    }

    load_history(sock, user_id);

    broadcast(name + " joined the chat\n",sock);

    //message receiving loop
    while ((n = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0)
    {
        buffer[n] = '\0';
        string msg(buffer);
        msg.erase(msg.find_last_not_of("\n\r") + 1);

        //save message 
        save_message(user_id, 0, msg);
        broadcast("[" + name + "]: " + msg + "\n", sock);
    }
    //disconnect
    {
        lock_guard<mutex> lock(name_mutex);
        client_names.erase(name);
        client_user_id.erase(name);
    }

    {
        lock_guard<mutex> lock(socket_mutex);
        //client_sockets.erase(
            // remove(client_sockets.begin(), client_sockets.end(), sock),
            // client_sockets.end());
    }

    broadcast(name + " left the chat\n");
    closesocket(sock);
}

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
        {
            {
                lock_guard<mutex> lock(socket_mutex);
                //client_sockets.push_back(client);
            }
            thread(handle_client, client).detach();
        }
    }
    sqlite3_close(db);
}
