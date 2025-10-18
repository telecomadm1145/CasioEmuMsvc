#include "GDBServer.hpp"
#include "../Emulator.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <sstream>

GDBServer::GDBServer(casioemu::Emulator* emu, int port)
    : m_emulator(emu), m_port(port), m_running(false), m_server_socket(INVALID_SOCKET), m_clientConnected(false)
{
}

GDBServer::~GDBServer()
{
    Stop();
}

bool GDBServer::Start()
{
    if (m_running)
        return true;

#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif

    m_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_server_socket == INVALID_SOCKET) {
#ifdef _WIN32
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
#else
        std::cerr << "Error at socket(): " << strerror(errno) << std::endl;
#endif
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = htonl(INADDR_ANY); 
    service.sin_port = htons(m_port);

    if (bind(m_server_socket, (sockaddr*)&service, sizeof(service)) == SOCKET_ERROR) {
        std::cerr << "bind() failed." << std::endl;
        close_socket_helper(m_server_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    if (listen(m_server_socket, 1) == SOCKET_ERROR) {
        std::cerr << "listen() failed." << std::endl;
        close_socket_helper(m_server_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    m_running = true;
    m_thread = std::thread(&GDBServer::ServerThread, this);

    std::cout << "GDB Server listening on port " << m_port << std::endl;

    return true;
}

void GDBServer::Stop()
{
    if (!m_running) return;

    m_running = false;
    if (m_server_socket != INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(m_server_socket, SD_BOTH);
#else
        shutdown(m_server_socket, SHUT_RDWR);
#endif
        close_socket_helper(m_server_socket);
        m_server_socket = INVALID_SOCKET;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

void GDBServer::ServerThread()
{
    while (m_running)
    {
        socket_t client_socket = accept(m_server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            if (m_running) {
                 std::cerr << "accept() failed or was interrupted." << std::endl;
            }
            break;
        }

        m_clientConnected = true;
        HandleConnection(client_socket);
        m_clientConnected = false;
        close_socket_helper(client_socket);
    }
}

void GDBServer::HandleConnection(socket_t client_socket)
{
    std::cout << "GDB client connected." << std::endl;
    std::string buffer;
    char c;
    
    send(client_socket, "+", 1, 0);

    while (true)
    {
        int bytesRead = recv(client_socket, &c, 1, 0);
        if (bytesRead <= 0) {
            break;
        }

        if (c == '$')
        {
            buffer.clear();
            while (recv(client_socket, &c, 1, 0) > 0 && c != '#')
            {
                buffer += c;
            }

            char checksum_str[3];
            if (recv(client_socket, checksum_str, 2, 0) == 2)
            {
                checksum_str[2] = '\0';
                unsigned int received_checksum;
                sscanf(checksum_str, "%x", &received_checksum);

                unsigned int calculated_checksum = 0;
                for (char ch : buffer)
                {
                    calculated_checksum = (calculated_checksum + (unsigned char)ch) % 256;
                }

                if (received_checksum == calculated_checksum)
                {
                    send(client_socket, "+", 1, 0); // ACK
                    ProcessPacket(client_socket, buffer);
                }
                else
                {
                    send(client_socket, "-", 1, 0); // NAK
                }
            } else {
                break;
            }
        }
    }
    std::cout << "GDB client disconnected." << std::endl;
}

void GDBServer::ProcessPacket(socket_t client_socket, const std::string& packet_data)
{
    if (packet_data.rfind("qSupported", 0) == 0)
    {
        const char* response_data = "PacketSize=400";
        
        std::string packet = "$";
        packet += response_data;
        packet += "#";

        unsigned int checksum = 0;
        for (char ch : std::string(response_data))
        {
            checksum = (checksum + (unsigned char)ch) % 256;
        }

        char checksum_str[3];
        snprintf(checksum_str, sizeof(checksum_str), "%02x", checksum);
        packet += checksum_str;

        send(client_socket, packet.c_str(), packet.length(), 0);
    }
    else
    {
        send(client_socket, "$#00", 4, 0);
    }
}

std::string GDBServer::GetStatus() const
{
    if (!m_running)
    {
        return "Stopped";
    }
    else if (m_clientConnected)
    {
        return "Client Connected";
    }
    else
    {
        return "Listening";
    }
}

int GDBServer::GetPort() const
{
    return m_port;
}