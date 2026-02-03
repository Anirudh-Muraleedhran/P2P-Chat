Compilation must be done on Developer command prompt 

client complilation command :
cl client.cpp sqlite3.c Ws2_32.lib /std:c++17 /EHsc

server complilation command :
cl server.cpp sqlite3.c Ws2_32.lib /std:c++17 /EHsc

running gui:
python gui_client.py