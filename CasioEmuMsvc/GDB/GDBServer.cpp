#include "GDBServer.hpp"
#include "../Emulator.hpp"
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <sstream>

#pragma comment(lib, "Ws2_32.lib")

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

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }

    m_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_server_socket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr("127.0.0.1");
    service.sin_port = htons(m_port);

    if (bind(m_server_socket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        std::cerr << "bind() failed." << std::endl;
        closesocket(m_server_socket);
        WSACleanup();
        return false;
    }

    if (listen(m_server_socket, 1) == SOCKET_ERROR) {
        std::cerr << "listen() failed." << std::endl;
        closesocket(m_server_socket);
        WSACleanup();
        return false;
    }

    m_running = true;
    m_thread = std::thread(&GDBServer::ServerThread, this);

    std::cout << "GDB Server listening on port " << m_port << std::endl;

    return true;
}

void GDBServer::Stop()
{
    m_running = false;
    if (m_server_socket != INVALID_SOCKET) {
        closesocket(m_server_socket);
        m_server_socket = INVALID_SOCKET;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    WSACleanup();
}

void GDBServer::ServerThread()
{
    while (m_running)
    {
        int client_socket = accept(m_server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            if (m_running) {
                 std::cerr << "accept() failed." << std::endl;
            }
            break;
        }

        m_clientConnected = true;
        HandleConnection(client_socket);
        m_clientConnected = false;
        closesocket(client_socket);
    }
}

void GDBServer::HandleConnection(int client_socket)
{
    std::cout << "GDB client connected." << std::endl;

    std::string buffer;
    char c;
    while (recv(client_socket, &c, 1, 0) > 0)
    {
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
                unsigned int received_checksum = std::stoul(checksum_str, nullptr, 16);

                unsigned int calculated_checksum = 0;
                for (char ch : buffer)
                {
                    calculated_checksum += ch;
                }
                calculated_checksum %= 256;

                if (received_checksum == calculated_checksum)
                {
                    send(client_socket, "+", 1, 0);
                    ProcessPacket(client_socket, buffer);
                }
                else
                {
                    send(client_socket, "-", 1, 0); // Nak
                }
            }
        }
    }

    std::cout << "GDB client disconnected." << std::endl;
}

void GDBServer::ProcessPacket(int client_socket, const std::string& packet_data)
{
    if (packet_data.rfind("qSupported", 0) == 0)
    {
        const char* response = "PacketSize=400";
        std::string packet = "$";
        packet += response;
        packet += "#";

        unsigned int checksum = 0;
        for (char ch : std::string(response))
        {
            checksum += ch;
        }
        checksum %= 256;

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
