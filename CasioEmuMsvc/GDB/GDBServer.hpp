#pragma once

#include <thread>
#include <atomic>
#include <string>

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

private:
    void ServerThread();
    void HandleConnection(int client_socket);
    void ProcessPacket(int client_socket, const std::string& packet_data);

    casioemu::Emulator* m_emulator;
    int m_port;
    std::thread m_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_clientConnected;
    int m_server_socket;
};
