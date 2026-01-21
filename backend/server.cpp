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

#include <map> //For storing Usernames->Sockets
#include <sstream> 

using namespace std;

#pragma comment(lib,"Ws2_32.lib") //tells the linker to include the Winsock library

#define PORT 5000
#define BUFFER_SIZE 1024 
#define DM_COMMAND_PREFIX "/dm|"
#define NAME_DELIMITER '|'


vector<SOCKET> client_Sockets; //global variable to store all the client sockets
mutex socket_mutex ;//protecting client_socket vertor
mutex name_mutex ; // mutex to protect the shared client_name map

//Function : broadcast_message
//Purpose : Broadcast the message to all the clients except the sender

// Maps client names to their active socket (used for DMs)
map<string, SOCKET> client_names;

void broadcast_message(SOCKET sender_socket,const char* buffer,int length)
{
    //USED FOR BROADCASTING FROM SENDER
    // lock_guard<mutex> lock(socket_mutex); //locked to safely iterate over the list

    // for(SOCKET client_Socket: client_Sockets)
    // {
    //     if(client_Socket != sender_socket)
    //     {
    //         if(send(client_Socket,buffer,length,0)==SOCKET_ERROR)
    //         {
    //             cerr<<"Send Error : "<<WSAGetLastError()<<endl;
    //         }
    //     }
    // }

    string sender_name = "Unknown";
    {
        lock_guard<mutex> lock(name_mutex);
        for(auto const& [name,sock] : client_names)
        {
            if(sock == sender_socket)
            {
                sender_name = name;
                break;
            }
        }
    }

    string broadcast_message = "[" +sender_name+"]: "+string(buffer,length)+"\n";

    //Broadcasting using the  map sockets 
    lock_guard<mutex> lock(name_mutex);
    for(auto const& [name,sock] : client_names)
    {
        if(sock != sender_socket)
        {
            if(send(sock,broadcast_message.c_str(),broadcast_message.length(),0)==SOCKET_ERROR)
            {
                cerr<<"[BROADCAST FAIL] Could not send to "<<name<<". Error : "<<WSAGetLastError()<<endl;
            }
        }
    }
}


//Function : Direct Message relaying

void relay_direct_message(const string& sender_name , const string& recipent_name,const string& message)
{
    //prints who the message is from 
    string formatted_message = "[DM from " + sender_name +"]:"+message+"\n";

    lock_guard<mutex>lock(name_mutex);
    auto it = client_names.find(recipent_name);

    //checking if the recipent exits and send if it does
    if(it != client_names.end())
    {
        SOCKET recipent_socket = it->second;

        if(send(recipent_socket,formatted_message.c_str(),formatted_message.length(),0)==SOCKET_ERROR)
        {
            cerr<<"[DM FAIL] Could not send to"<<recipent_name<<". Error: "<<WSAGetLastError()<<endl;
        }
        else
        {
            cout<<"[SERVER DM] Sent DM from : "<<sender_name<<" to : "<<recipent_name<<endl;
        }
    }
    else
    {
        cerr<<"[DM FAIL] Reciepent "<<recipent_name<<" NOT FOUND"<<endl;
    }
}



//Function: handle_peer_connection
// Purpose: Runs in a dedicated thread to continuously receive messages from ONE connected peer.

void handle_peer_connection(SOCKET client_socket)
{

    char buffer[BUFFER_SIZE];
    int read_size ;
    string client_name = "Unkonwn";

    //1. Registering the client name 

    read_size = recv(client_socket,buffer,BUFFER_SIZE-1,0);
    if(read_size >0)
    {
        buffer[read_size]= '\0';
        client_name = buffer;

        string name(buffer);
        //Removing the trailing \name
        name.erase(name.find_last_not_of("\n\r")+1);
        client_name = name ;

        //Adding names to the map 
        {
            lock_guard<mutex> lock(name_mutex);
            client_names[client_name] = client_socket;
        }

        string join_message = client_name + " has joined the chat.";
        broadcast_message(INVALID_SOCKET,join_message.c_str(),join_message.length());

        cout<<"\n[SERVER EVENT] Client "<<client_name<<" registered."<<endl;
    }

    //2. Main message loop 
    while((read_size = recv(client_socket,buffer,BUFFER_SIZE-1,0))>0)
    {
        buffer[read_size] = '\0';
        string received_data(buffer);

        received_data.erase(received_data.find_last_not_of("\n\r")+1);

        cout<<"\n[SERVER RECIEVED from "<<client_name<<"] : "<<received_data<<endl;

        //Listing all the usernames
        if(received_data=="/list")
        {
            string userlist = "[Server] User List :\n";
            {
                lock_guard<mutex> lock(name_mutex);
                for(auto const& [name,sock]: client_names)
                {
                    userlist += name + "|"; 
                }
            }
            userlist += "\n";
            send(client_socket,userlist.c_str(),userlist.length(),0);
            continue;
        }
        //Help Command
        else if(received_data=="/help")
        {
            string help_message = "[SERVER]: Commands:\n"
                      "/list - See online users\n"
                      "/dm|Name|Message - Send private message\n"
                      "exit - Disconnect\n";
            send(client_socket,help_message.c_str(),help_message.length(),0);
            continue;
        }

        //Checking for DM command "/dm|RecipientName|Message Content"
        else if(received_data.rfind(DM_COMMAND_PREFIX,0)==0)
        {
            size_t first_pipe = 3; //Index of first '|'
            size_t second_pipe = received_data.find(NAME_DELIMITER,first_pipe+1);
            //The recieved_data is of the format 
            //first pipe holds the index after "/dm|" i.e, the index of the recipent name
            //second pipe holds the index after the recipentname, it marks the begeining of the message 
            if(first_pipe != string::npos && second_pipe != string::npos)
            {
                string recipient_name = received_data.substr(first_pipe+1,second_pipe-first_pipe-1);
                string message = received_data.substr(second_pipe+1);

                relay_direct_message(client_name,recipient_name,message);
            }
            else
            {
                //ERROR
                string error_message = "[SERVER ERROR] DM syntax /dm|Recipient|Your message\n";
                send(client_socket,error_message.c_str(),error_message.length(),0);
            }
           
        }
        else if(received_data.length()>0 && received_data[0]=='/')
        {
            string error_message = "[SERVER ERROR]The use of / is incorrect. Type : /help for syntax \n";
            send(client_socket,error_message.c_str(),error_message.length(),0);
            continue;
        }
        else
        {
            broadcast_message(client_socket,received_data.c_str(),received_data.length());
        }
        memset(buffer,0,BUFFER_SIZE);

    }


    //Graceful disconnection
    if(read_size==0)
    {
        cout<<"\nClient "<<client_name<<" Disconnected gracefully."<<endl;
    }
    else{
        cerr<<"\nClient"<<client_name<<" Disconnected with ERROR : "<<WSAGetLastError()<<endl;
    }

    //Cleanup : Remove client from the map and close the socket 
    {
        lock_guard<mutex>lock(name_mutex);
        client_names.erase(client_name);//Romeves the keys by the consumer name
    }
    closesocket(client_socket);
}


//Fucntion: start_listening
//Purpose: Sets up the main listening socket

void start_listening()
{
    WSADATA wsadata; // structure to store the data
    SOCKET listen_socket = INVALID_SOCKET; //socket for handeling
    struct sockaddr_in service;
    int addrlen = sizeof(service);

    //1. initilizing the winsock DLL
    if(WSAStartup(MAKEWORD(2,2),&wsadata)!=0)
    {
        throw runtime_error("WSAStartup Failed!");
    }

    //2. Creatinga listening Socket 
    listen_socket = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(listen_socket==INVALID_SOCKET)
    {
        cerr<<"Socket Connection Failed : "<<WSAGetLastError()<<endl;
        WSACleanup();
        throw runtime_error("Socket Creation has Failed");
    }

    //3. Sockets to ports
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY; //listens to all local networks
    service.sin_port = htons(PORT); //host byte order to network byte order

    if(bind(listen_socket,(SOCKADDR*)&service,sizeof(service))==SOCKET_ERROR)
    {
        cerr<<"Bind Fail: "<<WSAGetLastError()<<endl;
        closesocket(listen_socket);
        WSACleanup();
        throw runtime_error("Bind Failed");
    }

    //4. Start Listening

    if(listen(listen_socket,SOMAXCONN)==SOCKET_ERROR)//SOMAXCONN is the max length of the queue
    {
        cerr<<"Listen Failed : "<<WSAGetLastError()<<endl;
        closesocket(listen_socket);
        WSACleanup();
        throw runtime_error("Listening Failed ");
    }

    cout<<"Chat server Listening on PORT : "<<PORT<<". Waiting for clients ..."<<endl;

    //5. Accept Loop
    while(true)
    {
        SOCKET peer_scoket = accept(listen_socket,(SOCKADDR*)&service,&addrlen);
        if(peer_scoket==INVALID_SOCKET)
        {
            cerr<<"Accept Failed : "<<WSAGetLastError()<<endl;
            continue;
        }
    
        //Adding thenew client inthe global list 

        lock_guard<mutex> lock(socket_mutex);
        client_Sockets.push_back(peer_scoket);

    
        //Connected Successfully , Displaying the client IP
        char * peer_ip = inet_ntoa(service.sin_addr); //Converts binary IP to dotted string
        cout<<"\nNew client connected from : "<<peer_ip<<"."<<endl;
        cout<<"\n[SERVER STATUS] Total Clients : "<<client_Sockets.size()<<endl;

        //New thread for each client
        
        thread handler_thread(handle_peer_connection,peer_scoket);
        //The new thread handles the communication with the peer  
        handler_thread.detach();
        //Detatching the thread will clear its resources 
    }

    closesocket(listen_socket);
    WSACleanup();
}

//TEST

int main()
{
    try{
        start_listening();
    }
    catch(const runtime_error& e){
        cerr<<"Application Terminated due to error : "<<e.what()<<endl;
        return 1;
    }
    return 0;
}
