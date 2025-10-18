#pragma once

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>

    #pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <errno.h>
    #include <string.h>
    typedef int socket_t;
    const int INVALID_SOCKET = -1;
    const int SOCKET_ERROR = -1;
#endif

inline void close_socket_helper(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}