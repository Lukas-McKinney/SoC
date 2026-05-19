#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
typedef SOCKET RelaySocket;
typedef int RelaySockLen;
#define RELAY_INVALID_SOCKET INVALID_SOCKET
#define RELAY_SOCKET_ERROR SOCKET_ERROR
#define relay_close_socket closesocket
#define relay_last_error_code() WSAGetLastError()
#define RELAY_WOULD_BLOCK(errorCode) ((errorCode) == WSAEWOULDBLOCK)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int RelaySocket;
typedef socklen_t RelaySockLen;
#define RELAY_INVALID_SOCKET (-1)
#define RELAY_SOCKET_ERROR (-1)
#define relay_close_socket close
#define relay_last_error_code() errno
#define RELAY_WOULD_BLOCK(errorCode) ((errorCode) == EWOULDBLOCK || (errorCode) == EAGAIN)
#endif

#define RELAY_MAX_CLIENTS 32
#define RELAY_MAX_ROOM_CODE 31
#define RELAY_RECV_BUFFER_SIZE 4096
#define RELAY_PENDING_BUFFER_SIZE 131072

enum RelayRole
{
    RELAY_ROLE_NONE,
    RELAY_ROLE_HOST,
    RELAY_ROLE_CLIENT
};

struct RelayClient
{
    RelaySocket socketHandle;
    bool inUse;
    bool handshakeComplete;
    bool paired;
    enum RelayRole role;
    char roomCode[RELAY_MAX_ROOM_CODE + 1];
    int peerIndex;
    unsigned char recvBuffer[RELAY_RECV_BUFFER_SIZE];
    size_t recvLength;
    unsigned char pendingBuffer[RELAY_PENDING_BUFFER_SIZE];
    size_t pendingLength;
};

static bool gSocketLayerReady = false;

static bool relay_socket_layer_init(void);
static bool set_socket_nonblocking(RelaySocket socketHandle);
static void set_socket_nodelay(RelaySocket socketHandle);
static void close_socket_if_open(RelaySocket *socketHandle);
static void disconnect_client(struct RelayClient *clients, int index, const char *reason);
static void pair_room(struct RelayClient *clients, int index);
static void flush_client(struct RelayClient *clients, int index);
static void process_client_input(struct RelayClient *clients, int index);
static int find_free_client_slot(const struct RelayClient *clients);
static bool parse_handshake_line(const char *line, char *roomCode, size_t roomCodeSize, enum RelayRole *role);
static void normalize_handshake_line(char *line);

static bool relay_socket_layer_init(void)
{
#ifdef _WIN32
    if (!gSocketLayerReady)
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            return false;
        }
        gSocketLayerReady = true;
    }
#endif
    return true;
}

static bool set_socket_nonblocking(RelaySocket socketHandle)
{
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socketHandle, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socketHandle, F_GETFL, 0);
    if (flags < 0)
    {
        return false;
    }

    return fcntl(socketHandle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void set_socket_nodelay(RelaySocket socketHandle)
{
    int enabled = 1;
    setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled, (RelaySockLen)sizeof(enabled));
}

static void close_socket_if_open(RelaySocket *socketHandle)
{
    if (socketHandle == NULL || *socketHandle == RELAY_INVALID_SOCKET)
    {
        return;
    }

    relay_close_socket(*socketHandle);
    *socketHandle = RELAY_INVALID_SOCKET;
}

static int find_free_client_slot(const struct RelayClient *clients)
{
    for (int i = 0; i < RELAY_MAX_CLIENTS; i++)
    {
        if (!clients[i].inUse)
        {
            return i;
        }
    }

    return -1;
}

static void normalize_handshake_line(char *line)
{
    size_t length = 0u;

    if (line == NULL)
    {
        return;
    }

    length = strlen(line);
    while (length > 0u && (line[length - 1u] == '\n' || line[length - 1u] == '\r' || line[length - 1u] == ' ' || line[length - 1u] == '\t'))
    {
        line[length - 1u] = '\0';
        length--;
    }
}

static bool parse_handshake_line(const char *line, char *roomCode, size_t roomCodeSize, enum RelayRole *role)
{
    char version[8];
    char parsedRoom[RELAY_MAX_ROOM_CODE + 1];
    char roleText[16];

    if (line == NULL || roomCode == NULL || role == NULL)
    {
        return false;
    }

    if (sscanf(line, "SOC-RELAY %7s %31s %15s", version, parsedRoom, roleText) != 3)
    {
        return false;
    }

    if (strcmp(version, "1") != 0)
    {
        return false;
    }

    if (strcmp(roleText, "HOST") == 0)
    {
        *role = RELAY_ROLE_HOST;
    }
    else if (strcmp(roleText, "CLIENT") == 0)
    {
        *role = RELAY_ROLE_CLIENT;
    }
    else
    {
        return false;
    }

    snprintf(roomCode, roomCodeSize, "%s", parsedRoom);
    return true;
}

static void disconnect_client(struct RelayClient *clients, int index, const char *reason)
{
    int peerIndex = -1;

    (void)reason;
    if (clients == NULL || index < 0 || index >= RELAY_MAX_CLIENTS || !clients[index].inUse)
    {
        return;
    }

    peerIndex = clients[index].peerIndex;
    close_socket_if_open(&clients[index].socketHandle);
    clients[index].inUse = false;
    clients[index].handshakeComplete = false;
    clients[index].paired = false;
    clients[index].role = RELAY_ROLE_NONE;
    clients[index].roomCode[0] = '\0';
    clients[index].peerIndex = -1;
    clients[index].recvLength = 0u;
    clients[index].pendingLength = 0u;

    if (peerIndex >= 0 && peerIndex < RELAY_MAX_CLIENTS && clients[peerIndex].inUse)
    {
        close_socket_if_open(&clients[peerIndex].socketHandle);
        clients[peerIndex].inUse = false;
        clients[peerIndex].handshakeComplete = false;
        clients[peerIndex].paired = false;
        clients[peerIndex].role = RELAY_ROLE_NONE;
        clients[peerIndex].roomCode[0] = '\0';
        clients[peerIndex].peerIndex = -1;
        clients[peerIndex].recvLength = 0u;
        clients[peerIndex].pendingLength = 0u;
    }
}

static void flush_client(struct RelayClient *clients, int index)
{
    struct RelayClient *client = NULL;
    struct RelayClient *peer = NULL;
    int sent = 0;

    if (clients == NULL || index < 0 || index >= RELAY_MAX_CLIENTS)
    {
        return;
    }

    client = &clients[index];
    if (!client->inUse || !client->paired || client->peerIndex < 0 || client->peerIndex >= RELAY_MAX_CLIENTS)
    {
        return;
    }

    peer = &clients[client->peerIndex];
    if (!peer->inUse || peer->socketHandle == RELAY_INVALID_SOCKET || client->pendingLength == 0u)
    {
        return;
    }

    sent = send(peer->socketHandle, (const char *)client->pendingBuffer, (int)client->pendingLength, 0);
    if (sent == RELAY_SOCKET_ERROR)
    {
        const int errorCode = relay_last_error_code();
        if (RELAY_WOULD_BLOCK(errorCode))
        {
            return;
        }
        disconnect_client(clients, index, "send failed");
        return;
    }

    if (sent <= 0)
    {
        disconnect_client(clients, index, "peer closed");
        return;
    }

    if ((size_t)sent < client->pendingLength)
    {
        memmove(client->pendingBuffer, client->pendingBuffer + sent, client->pendingLength - (size_t)sent);
    }
    client->pendingLength -= (size_t)sent;
}

static void pair_room(struct RelayClient *clients, int index)
{
    int peerIndex = -1;
    struct RelayClient *client = NULL;

    if (clients == NULL || index < 0 || index >= RELAY_MAX_CLIENTS || !clients[index].inUse || !clients[index].handshakeComplete || clients[index].paired)
    {
        return;
    }

    client = &clients[index];
    for (int i = 0; i < RELAY_MAX_CLIENTS; i++)
    {
        if (i == index || !clients[i].inUse || !clients[i].handshakeComplete || clients[i].paired)
        {
            continue;
        }

        if (clients[i].role == RELAY_ROLE_NONE || clients[i].role == client->role)
        {
            continue;
        }

        if (strcmp(clients[i].roomCode, client->roomCode) != 0)
        {
            continue;
        }

        peerIndex = i;
        break;
    }

    if (peerIndex < 0)
    {
        return;
    }

    client->paired = true;
    client->peerIndex = peerIndex;
    clients[peerIndex].paired = true;
    clients[peerIndex].peerIndex = index;

    flush_client(clients, index);
    flush_client(clients, peerIndex);
}

static void process_client_input(struct RelayClient *clients, int index)
{
    struct RelayClient *client = NULL;
    char *newline = NULL;
    int received = 0;

    if (clients == NULL || index < 0 || index >= RELAY_MAX_CLIENTS)
    {
        return;
    }

    client = &clients[index];
    if (!client->inUse || client->socketHandle == RELAY_INVALID_SOCKET)
    {
        return;
    }

    while (true)
    {
        const int remaining = (int)(sizeof(client->recvBuffer) - client->recvLength);
        if (remaining <= 0)
        {
            disconnect_client(clients, index, "handshake too long");
            return;
        }

        received = recv(client->socketHandle, (char *)(client->recvBuffer + client->recvLength), remaining, 0);
        if (received == 0)
        {
            disconnect_client(clients, index, "peer disconnected");
            return;
        }

        if (received == RELAY_SOCKET_ERROR)
        {
            const int errorCode = relay_last_error_code();
            if (RELAY_WOULD_BLOCK(errorCode))
            {
                return;
            }
            disconnect_client(clients, index, "receive failed");
            return;
        }

        if (received < 0)
        {
            return;
        }

        client->recvLength += (size_t)received;

        if (!client->handshakeComplete)
        {
            newline = memchr(client->recvBuffer, '\n', client->recvLength);
            if (newline == NULL)
            {
                continue;
            }

            {
                char line[128];
                size_t lineLength = (size_t)(newline - (char *)client->recvBuffer);
                char roomCode[RELAY_MAX_ROOM_CODE + 1];
                enum RelayRole role = RELAY_ROLE_NONE;
                size_t leftover = client->recvLength - lineLength - 1u;

                if (lineLength >= sizeof(line))
                {
                    disconnect_client(clients, index, "handshake line too long");
                    return;
                }

                memcpy(line, client->recvBuffer, lineLength);
                line[lineLength] = '\0';
                normalize_handshake_line(line);
                if (!parse_handshake_line(line, roomCode, sizeof(roomCode), &role))
                {
                    disconnect_client(clients, index, "invalid handshake");
                    return;
                }

                client->handshakeComplete = true;
                client->role = role;
                snprintf(client->roomCode, sizeof(client->roomCode), "%s", roomCode);

                if (leftover > 0u)
                {
                    if (leftover > sizeof(client->pendingBuffer) - client->pendingLength)
                    {
                        disconnect_client(clients, index, "buffer overflow");
                        return;
                    }
                    memcpy(client->pendingBuffer + client->pendingLength, newline + 1, leftover);
                    client->pendingLength += leftover;
                }
                client->recvLength = 0u;
                pair_room(clients, index);
            }
        }
        else
        {
            if ((size_t)received > sizeof(client->pendingBuffer) - client->pendingLength)
            {
                disconnect_client(clients, index, "buffer overflow");
                return;
            }

            memcpy(client->pendingBuffer + client->pendingLength,
                   client->recvBuffer + (client->recvLength - (size_t)received),
                   (size_t)received);
            client->pendingLength += (size_t)received;
            client->recvLength = 0u;

            if (client->paired)
            {
                flush_client(clients, index);
            }
        }
    }
}

int main(int argc, char **argv)
{
    RelaySocket listenSocket = RELAY_INVALID_SOCKET;
    struct sockaddr_in listenAddress;
    struct RelayClient clients[RELAY_MAX_CLIENTS];
    unsigned short port = 24680u;
    const char *portEnv = getenv("PORT");
    fd_set readSet;
    fd_set writeSet;
    int maxSocket = 0;

    if (portEnv != NULL && portEnv[0] != '\0')
    {
        long parsedPort = strtol(portEnv, NULL, 10);
        if (parsedPort > 0 && parsedPort <= 65535)
        {
            port = (unsigned short)parsedPort;
        }
    }
    else if (argc > 1)
    {
        long parsedPort = strtol(argv[1], NULL, 10);
        if (parsedPort > 0 && parsedPort <= 65535)
        {
            port = (unsigned short)parsedPort;
        }
    }

    if (!relay_socket_layer_init())
    {
        fprintf(stderr, "failed to initialize socket layer\n");
        return 1;
    }

    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < RELAY_MAX_CLIENTS; i++)
    {
        clients[i].socketHandle = RELAY_INVALID_SOCKET;
        clients[i].peerIndex = -1;
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == RELAY_INVALID_SOCKET)
    {
        fprintf(stderr, "socket failed\n");
        return 1;
    }

    {
        int reuseAddress = 1;
        setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuseAddress, (RelaySockLen)sizeof(reuseAddress));
    }

    if (!set_socket_nonblocking(listenSocket))
    {
        fprintf(stderr, "failed to set nonblocking mode\n");
        close_socket_if_open(&listenSocket);
        return 1;
    }

    memset(&listenAddress, 0, sizeof(listenAddress));
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    listenAddress.sin_port = htons(port);

    if (bind(listenSocket, (struct sockaddr *)&listenAddress, sizeof(listenAddress)) == RELAY_SOCKET_ERROR)
    {
        fprintf(stderr, "bind failed on port %u\n", (unsigned int)port);
        close_socket_if_open(&listenSocket);
        return 1;
    }

    if (listen(listenSocket, RELAY_MAX_CLIENTS) == RELAY_SOCKET_ERROR)
    {
        fprintf(stderr, "listen failed\n");
        close_socket_if_open(&listenSocket);
        return 1;
    }

    printf("SoC relay server listening on port %u\n", (unsigned int)port);
    fflush(stdout);

    while (true)
    {
        struct timeval timeout;
        maxSocket = (int)listenSocket;

        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_SET(listenSocket, &readSet);

        for (int i = 0; i < RELAY_MAX_CLIENTS; i++)
        {
            if (!clients[i].inUse || clients[i].socketHandle == RELAY_INVALID_SOCKET)
            {
                continue;
            }

            FD_SET(clients[i].socketHandle, &readSet);
            if (clients[i].paired && clients[i].pendingLength > 0u)
            {
                FD_SET(clients[i].socketHandle, &writeSet);
            }
            if ((int)clients[i].socketHandle > maxSocket)
            {
                maxSocket = (int)clients[i].socketHandle;
            }
        }

        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        if (select(maxSocket + 1, &readSet, &writeSet, NULL, &timeout) < 0)
        {
            const int errorCode = relay_last_error_code();
            if (errorCode != EINTR)
            {
                fprintf(stderr, "select failed\n");
                break;
            }
        }

        if (FD_ISSET(listenSocket, &readSet))
        {
            while (true)
            {
                struct sockaddr_in clientAddress;
                RelaySockLen addressLength = (RelaySockLen)sizeof(clientAddress);
                RelaySocket clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddress, &addressLength);
                int slot = -1;

                if (clientSocket == RELAY_INVALID_SOCKET)
                {
                    const int errorCode = relay_last_error_code();
                    if (!RELAY_WOULD_BLOCK(errorCode))
                    {
                        fprintf(stderr, "accept failed\n");
                    }
                    break;
                }

                slot = find_free_client_slot(clients);
                if (slot < 0)
                {
                    relay_close_socket(clientSocket);
                    continue;
                }

                if (!set_socket_nonblocking(clientSocket))
                {
                    relay_close_socket(clientSocket);
                    continue;
                }

                set_socket_nodelay(clientSocket);
                clients[slot].socketHandle = clientSocket;
                clients[slot].inUse = true;
                clients[slot].handshakeComplete = false;
                clients[slot].paired = false;
                clients[slot].role = RELAY_ROLE_NONE;
                clients[slot].roomCode[0] = '\0';
                clients[slot].peerIndex = -1;
                clients[slot].recvLength = 0u;
                clients[slot].pendingLength = 0u;
            }
        }

        for (int i = 0; i < RELAY_MAX_CLIENTS; i++)
        {
            if (!clients[i].inUse || clients[i].socketHandle == RELAY_INVALID_SOCKET)
            {
                continue;
            }

            if (FD_ISSET(clients[i].socketHandle, &readSet))
            {
                process_client_input(clients, i);
            }

            if (FD_ISSET(clients[i].socketHandle, &writeSet))
            {
                flush_client(clients, i);
            }
        }
    }

    close_socket_if_open(&listenSocket);
    for (int i = 0; i < RELAY_MAX_CLIENTS; i++)
    {
        close_socket_if_open(&clients[i].socketHandle);
    }

#ifdef _WIN32
    if (gSocketLayerReady)
    {
        WSACleanup();
        gSocketLayerReady = false;
    }
#endif

    return 0;
}
