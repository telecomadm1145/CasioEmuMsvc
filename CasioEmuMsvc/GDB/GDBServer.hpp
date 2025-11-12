#pragma once

#include "NetCompat.hpp"
#include <thread>
#include <atomic>
#include <string>
#include <vector>

namespace casioemu {
    class Emulator;
}

class GDBServer
{
public:
    GDBServer(casioemu::Emulator* emu, int port);
    ~GDBServer();
    bool Start();
    void Stop();
    std::string GetStatus() const;
    int GetPort() const;
    void SetPort(int port);

private:
    void ServerThread();
    void HandleConnection(socket_t client_socket);
    void ProcessPacket(socket_t client_socket, const std::string& packet_data);
    casioemu::Emulator* m_emulator;
    int m_port;
    std::thread m_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_clientConnected;
    socket_t m_server_socket;
};