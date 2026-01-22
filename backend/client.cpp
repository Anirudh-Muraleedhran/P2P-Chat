#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <cstring>

using namespace std;

#pragma comment(lib,"Ws2_32.lib")

#define PORT 5000
#define BUFFER_SIZE 1024

void receive_messages(SOCKET server_socket)
{
    char buffer[BUFFER_SIZE];
    int read_size;

    while ((read_size = recv(server_socket, buffer, BUFFER_SIZE - 1, 0)) > 0)
    {
        buffer[read_size] = '\0';
        cout << "\n" << buffer << "> " << flush;
    }

    cout << "\n[Disconnected from server]\n";
}

void start_sending(const string& target_ip)
{
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET)
    {
        cerr << "Socket creation failed\n";
        return;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr);

    if (connect(client_socket, (SOCKADDR*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        cerr << "Connection failed\n";
        closesocket(client_socket);
        return;
    }

    string username;
    do
    {
        cout << "Username: ";
        getline(cin, username);
    } while (username.empty());

    username += "\n";
    send(client_socket, username.c_str(), username.length(), 0);

    thread receiver(receive_messages, client_socket);
    receiver.detach();

    cout << "> ";
    string msg;
    while (getline(cin, msg))
    {
        if (msg == "exit")
            break;

        msg += "\n";
        send(client_socket, msg.c_str(), msg.length(), 0);
        cout << "> ";
    }

    closesocket(client_socket);
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        cerr << "Usage: client <server_ip>\n";
        return 1;
    }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    start_sending(argv[1]);

    WSACleanup();
    return 0;
}
