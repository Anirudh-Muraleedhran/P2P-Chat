#include <iostream>        // Standard C++ input/output operations
#include <string>          // Standard string library
#include <winsock2.h>      // Primary header for Winsock functions and types. store information about a given host
#include <ws2tcpip.h>      // Contains functions like inet_pton (Windows version)
#include <thread>          // C++ standard library for multithreading
#include <stdexcept>       // Standard exception handling
#include <cstring>         // For C-style string functions like memset

#include <vector>  // To store multiple client sockets
#include <mutex>   //To protect the shared vector
#include <algorithm> //For std::remove


using namespace std;

#pragma comment(lib,"Ws2_32.lib") //tells the linker to include the Winsock library

#define PORT 5000
#define BUFFER_SIZE 1024

//Function : recieve_messages
//Purpose : Handles recieving the messages from the server in seperate threads

void recieve_messages(SOCKET server_socket)
{
    char buffer[BUFFER_SIZE];
    int read_size;

    //Looping continuously to recieve the data from the server
    while((read_size = recv(server_socket,buffer,BUFFER_SIZE-1,0))>0)
    {
        buffer[read_size] = '\0';

        cerr<<"\r[MESSAGE RECIEVED]: "<<buffer<<endl;
        cout<<"> "<<flush;

        memset(buffer,0,BUFFER_SIZE);
    }

    //If the server is disconnected read_size<=0
    if(read_size==0)
    {
        cerr<<"\rServer disconnected gracefully."<<endl;
    }
    else if(read_size==SOCKET_ERROR)
    {
        cerr<<"\rRecieve failed (Server side error): "<<WSAGetLastError()<<endl;
    }
}



//Function : start_sending
//Purpose : Initiates connection to a remote peer and handles the inpt given by the user 

void start_sending(const string &target_ip)
{
    SOCKET client_socket = INVALID_SOCKET;
    struct sockaddr_in client_service;
    string message ;

    //1.Creating the client socket
    client_socket = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(client_socket == INVALID_SOCKET)
    {
        cerr<<"\nClient socket creation error :"<<WSAGetLastError()<<endl;
        return ;
    }

    //2. Traget Address

    client_service.sin_family = AF_INET;
    client_service.sin_port = htons(PORT); //host byte order to network byte order

    if(inet_pton(AF_INET,target_ip.c_str(),&client_service.sin_addr)<=0)
    {
        cerr<<"\nInvalidIP address: "<<target_ip<<endl;
        closesocket(client_socket);
        return ;
    }

    //3. Connect to the target peer's listener
    if(connect(client_socket,(SOCKADDR*)&client_service,sizeof(client_service))==SOCKET_ERROR)
    {
        cerr<<"\nConnection Failed to: "<<target_ip<<". Error : "<<WSAGetLastError()<<endl;
        closesocket(client_socket);
        return ;
    }

    cout<<"\nSuccessfully connected to the peer : "<<target_ip<<endl;


    //Creating threads to recieve the mesages from the server
    thread reciver_thread(recieve_messages,client_socket);
    reciver_thread.detach();

    cout<<"---Start Converstaion---"<<endl;
    cout<<"Type 'exit' to disconnect"<<endl;

    //4. Sending loop
    cout<<"> "<<flush;
    while(getline(cin,message))
    {
        if(message == "exit")
        {
            break;
        }

        string full_message = message +"\n";

        if(send(client_socket,full_message.c_str(),(int)full_message.length(),0)==SOCKET_ERROR)
        {
            cerr<<"\nSend Failed.Disconnecting."<<endl;
            break;
        }
        cout<< "> " <<flush;
    }

    closesocket(client_socket);
    cout<<"\nDisconnected from peer : "<<target_ip<<endl;
}

int main(int argc,const char *argv[])
{
    if(argc!=2)
    {
        cerr<<"Usage : "<<argv[0]<<" <Server_IP>"<<endl;
        return 1;
    }

    WSADATA WSAData;
    if(WSAStartup(MAKEWORD(2,2),&WSAData)!=0)
    {
        cerr<<"WSA Startup FAiled"<<endl;
        return 1;
    }

    string target_ip = argv[1];
    start_sending(target_ip);

    WSACleanup();
    return 0;
}
