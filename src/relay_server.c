#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

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

enum RelayTransportMode
{
    RELAY_TRANSPORT_UNKNOWN,
    RELAY_TRANSPORT_TCP,
    RELAY_TRANSPORT_WEBSOCKET
};

struct RelayClient
{
    RelaySocket socketHandle;
    bool inUse;
    bool handshakeComplete;
    bool paired;
    enum RelayTransportMode transportMode;
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
static bool try_read_http_request_path(const char *requestText, char *path, size_t pathSize);
static const char *find_http_header_value(const char *requestText, const char *headerName);
static bool parse_websocket_request(const char *requestText, char *roomCode, size_t roomCodeSize, enum RelayRole *role, char *acceptKey, size_t acceptKeySize);
static bool send_websocket_upgrade_response(RelaySocket socketHandle, const char *acceptKey);
static bool send_http_text_response(RelaySocket socketHandle, int statusCode, const char *reasonPhrase, const char *body);
static bool websocket_frame_append(struct RelayClient *clients, int index, const unsigned char *payload, size_t payloadSize);
static bool websocket_frame_send(RelaySocket socketHandle, unsigned char opcode, const unsigned char *payload, size_t payloadSize);
static bool websocket_frame_parse(struct RelayClient *clients, int index);

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

static bool send_all_socket(RelaySocket socketHandle, const unsigned char *buffer, size_t length)
{
    size_t sentTotal = 0u;

    while (sentTotal < length)
    {
        const int sent = send(socketHandle, (const char *)(buffer + sentTotal), (int)(length - sentTotal), 0);
        if (sent == RELAY_SOCKET_ERROR)
        {
            const int errorCode = relay_last_error_code();
            if (RELAY_WOULD_BLOCK(errorCode))
            {
                return false;
            }
            return false;
        }

        if (sent <= 0)
        {
            return false;
        }

        sentTotal += (size_t)sent;
    }

    return true;
}

/* SHA1 helpers are provided by src/websocket.c; remove local duplicates. */

#include "websocket.h"

static bool copy_query_value(const char *path, const char *name, char *buffer, size_t bufferSize)
{
    const char *query = NULL;
    size_t nameLength = 0u;

    if (path == NULL || name == NULL || buffer == NULL || bufferSize == 0u)
    {
        return false;
    }

    query = strchr(path, '?');
    if (query == NULL)
    {
        return false;
    }

    nameLength = strlen(name);
    query++;
    while (*query != '\0')
    {
        const char *equals = strchr(query, '=');
        const char *ampersand = strchr(query, '&');
        size_t keyLength = 0u;
        size_t valueLength = 0u;

        if (equals == NULL)
        {
            break;
        }

        if (ampersand != NULL && ampersand < equals)
        {
            query = ampersand + 1;
            continue;
        }

        keyLength = (size_t)(equals - query);
        if (keyLength == nameLength && strncmp(query, name, nameLength) == 0)
        {
            const char *valueStart = equals + 1;
            const char *valueEnd = ampersand != NULL ? ampersand : query + strlen(query);
            valueLength = (size_t)(valueEnd - valueStart);
            if (valueLength >= bufferSize)
            {
                return false;
            }

            memcpy(buffer, valueStart, valueLength);
            buffer[valueLength] = '\0';
            return true;
        }

        if (ampersand == NULL)
        {
            break;
        }
        query = ampersand + 1;
    }

    return false;
}

static bool try_read_http_request_path(const char *requestText, char *path, size_t pathSize)
{
    char requestLine[256];
    char parsedPath[256];
    const char *lineEnd = NULL;

    if (requestText == NULL || path == NULL || pathSize == 0u)
    {
        return false;
    }

    lineEnd = strstr(requestText, "\r\n");
    if (lineEnd == NULL || (size_t)(lineEnd - requestText) >= sizeof(requestLine))
    {
        return false;
    }

    memcpy(requestLine, requestText, (size_t)(lineEnd - requestText));
    requestLine[lineEnd - requestText] = '\0';
    if (sscanf(requestLine, "GET %255s", parsedPath) != 1)
    {
        return false;
    }

    if (strlen(parsedPath) >= pathSize)
    {
        return false;
    }

    snprintf(path, pathSize, "%s", parsedPath);
    return true;
}

static bool header_name_matches(const char *line, size_t lineLength, const char *headerName, size_t headerNameLength)
{
    if (line == NULL || headerName == NULL || lineLength <= headerNameLength || line[headerNameLength] != ':')
    {
        return false;
    }

    for (size_t i = 0u; i < headerNameLength; i++)
    {
        if (tolower((unsigned char)line[i]) != tolower((unsigned char)headerName[i]))
        {
            return false;
        }
    }

    return true;
}

static const char *find_http_header_value(const char *requestText, const char *headerName)
{
    const char *cursor = NULL;
    size_t headerNameLength = 0u;

    if (requestText == NULL || headerName == NULL)
    {
        return NULL;
    }

    cursor = strstr(requestText, "\r\n");
    if (cursor == NULL)
    {
        return NULL;
    }

    headerNameLength = strlen(headerName);
    cursor += 2;
    while (*cursor != '\0')
    {
        const char *lineEnd = strstr(cursor, "\r\n");
        size_t lineLength = 0u;

        if (lineEnd == NULL)
        {
            return NULL;
        }

        if (lineEnd == cursor)
        {
            return NULL;
        }

        lineLength = (size_t)(lineEnd - cursor);
        if (header_name_matches(cursor, lineLength, headerName, headerNameLength))
        {
            const char *value = cursor + headerNameLength + 1u;
            while (*value == ' ' || *value == '\t')
            {
                value++;
            }
            return value;
        }

        cursor = lineEnd + 2;
    }

    return NULL;
}

static bool parse_websocket_request(const char *requestText, char *roomCode, size_t roomCodeSize, enum RelayRole *role, char *acceptKey, size_t acceptKeySize)
{
    const char *pathStart = NULL;
    const char *pathEnd = NULL;
    char path[128];
    char roleText[16];

    if (requestText == NULL || roomCode == NULL || role == NULL || acceptKey == NULL)
    {
        return false;
    }

    if (!try_read_http_request_path(requestText, path, sizeof(path)))
    {
        return false;
    }

    if (!copy_query_value(path, "room", roomCode, roomCodeSize))
    {
        snprintf(roomCode, roomCodeSize, "%s", "default");
    }

    if (!copy_query_value(path, "role", roleText, sizeof(roleText)))
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

    pathStart = find_http_header_value(requestText, "Sec-WebSocket-Key");
    if (pathStart == NULL)
    {
        return false;
    }

    pathEnd = strstr(pathStart, "\r\n");
    if (pathEnd == NULL || pathEnd <= pathStart)
    {
        return false;
    }

    if ((size_t)(pathEnd - pathStart) >= acceptKeySize)
    {
        return false;
    }

    memcpy(acceptKey, pathStart, (size_t)(pathEnd - pathStart));
    acceptKey[pathEnd - pathStart] = '\0';
    return true;
}

static bool send_http_text_response(RelaySocket socketHandle, int statusCode, const char *reasonPhrase, const char *body)
{
    char response[512];
    const char *payload = body != NULL ? body : "";
    int responseLength = 0;

    if (reasonPhrase == NULL)
    {
        return false;
    }

    responseLength = snprintf(response,
                              sizeof(response),
                              "HTTP/1.1 %d %s\r\n"
                              "Content-Type: text/plain; charset=utf-8\r\n"
                              "Content-Length: %u\r\n"
                              "Connection: close\r\n\r\n"
                              "%s",
                              statusCode,
                              reasonPhrase,
                              (unsigned int)strlen(payload),
                              payload);
    if (responseLength <= 0 || (size_t)responseLength >= sizeof(response))
    {
        return false;
    }

    return send_all_socket(socketHandle, (const unsigned char *)response, (size_t)responseLength);
}

static bool send_websocket_upgrade_response(RelaySocket socketHandle, const char *acceptKey)
{
    char response[256];
    char computedAcceptKey[128];
    int responseLength = 0;

    if (acceptKey == NULL)
    {
        return false;
    }

    if (!websocket_accept_key(acceptKey, computedAcceptKey, sizeof(computedAcceptKey)))
    {
        return false;
    }

    responseLength = snprintf(response,
                              sizeof(response),
                              "HTTP/1.1 101 Switching Protocols\r\n"
                              "Upgrade: websocket\r\n"
                              "Connection: Upgrade\r\n"
                              "Sec-WebSocket-Accept: %s\r\n\r\n",
                              computedAcceptKey);
    if (responseLength <= 0 || (size_t)responseLength >= sizeof(response))
    {
        return false;
    }

    return send_all_socket(socketHandle, (const unsigned char *)response, (size_t)responseLength);
}

static bool websocket_frame_append(struct RelayClient *clients, int index, const unsigned char *payload, size_t payloadSize)
{
    struct RelayClient *client = NULL;

    if (clients == NULL || index < 0 || index >= RELAY_MAX_CLIENTS || payload == NULL)
    {
        return false;
    }

    client = &clients[index];
    if (payloadSize > sizeof(client->pendingBuffer) - client->pendingLength)
    {
        disconnect_client(clients, index, "buffer overflow");
        return false;
    }

    memcpy(client->pendingBuffer + client->pendingLength, payload, payloadSize);
    client->pendingLength += payloadSize;
    return true;
}

static bool websocket_frame_send(RelaySocket socketHandle, unsigned char opcode, const unsigned char *payload, size_t payloadSize)
{
    unsigned char header[10];
    size_t headerLength = 2u;

    header[0] = 0x80u | (opcode & 0x0Fu);
    if (payloadSize <= 125u)
    {
        header[1] = (unsigned char)payloadSize;
    }
    else if (payloadSize <= 65535u)
    {
        header[1] = 126u;
        header[2] = (unsigned char)((payloadSize >> 8) & 0xFFu);
        header[3] = (unsigned char)(payloadSize & 0xFFu);
        headerLength = 4u;
    }
    else
    {
        header[1] = 127u;
        header[2] = 0u;
        header[3] = 0u;
        header[4] = 0u;
        header[5] = 0u;
        header[6] = (unsigned char)((payloadSize >> 24) & 0xFFu);
        header[7] = (unsigned char)((payloadSize >> 16) & 0xFFu);
        header[8] = (unsigned char)((payloadSize >> 8) & 0xFFu);
        header[9] = (unsigned char)(payloadSize & 0xFFu);
        headerLength = 10u;
    }

    if (!send_all_socket(socketHandle, header, headerLength))
    {
        return false;
    }

    return payloadSize == 0u || send_all_socket(socketHandle, payload, payloadSize);
}

static bool websocket_frame_parse(struct RelayClient *clients, int index)
{
    struct RelayClient *client = NULL;

    if (clients == NULL || index < 0 || index >= RELAY_MAX_CLIENTS)
    {
        return false;
    }

    client = &clients[index];
    while (client->recvLength >= 2u)
    {
        const unsigned char *buffer = client->recvBuffer;
        const unsigned char firstByte = buffer[0];
        const unsigned char secondByte = buffer[1];
        const bool fin = (firstByte & 0x80u) != 0u;
        const unsigned char opcode = firstByte & 0x0Fu;
        const bool masked = (secondByte & 0x80u) != 0u;
        uint64_t payloadLength = (uint64_t)(secondByte & 0x7Fu);
        size_t headerLength = 2u;
        size_t payloadOffset = 0u;

        if (opcode == 0x8u)
        {
            disconnect_client(clients, index, "peer disconnected");
            return false;
        }

        if (opcode == 0x9u)
        {
            disconnect_client(clients, index, "ping unsupported");
            return false;
        }

        if (!fin || opcode == 0x0u)
        {
            disconnect_client(clients, index, "fragmented frame unsupported");
            return false;
        }

        if (!masked)
        {
            disconnect_client(clients, index, "unmasked frame");
            return false;
        }

        if (payloadLength == 126u)
        {
            if (client->recvLength < 4u)
            {
                return true;
            }
            payloadLength = ((uint64_t)buffer[2] << 8u) | (uint64_t)buffer[3];
            headerLength = 4u;
        }
        else if (payloadLength == 127u)
        {
            if (client->recvLength < 10u)
            {
                return true;
            }
            payloadLength = ((uint64_t)buffer[6] << 24u) |
                            ((uint64_t)buffer[7] << 16u) |
                            ((uint64_t)buffer[8] << 8u) |
                            (uint64_t)buffer[9];
            headerLength = 10u;
        }

        payloadOffset = headerLength + 4u;
        if (client->recvLength < payloadOffset + (size_t)payloadLength)
        {
            return true;
        }

        {
            unsigned char mask[4];
            unsigned char *payload = client->recvBuffer + payloadOffset;
            size_t payloadIndex = 0u;

            fprintf(stderr, "[relay] websocket frame parsed: idx=%d opcode=%u masked=%d payloadLength=%llu\n", index, opcode, masked, (unsigned long long)payloadLength);
            fflush(stderr);

            mask[0] = buffer[headerLength + 0u];
            mask[1] = buffer[headerLength + 1u];
            mask[2] = buffer[headerLength + 2u];
            mask[3] = buffer[headerLength + 3u];

            for (; payloadIndex < (size_t)payloadLength; payloadIndex++)
            {
                payload[payloadIndex] ^= mask[payloadIndex % 4u];
            }

            fprintf(stderr, "[relay] websocket frame unmasked: idx=%d payloadLen=%zu\n", index, (size_t)payloadLength);
            fflush(stderr);

            if (!websocket_frame_append(clients, index, payload, (size_t)payloadLength))
            {
                return false;
            }

            memmove(client->recvBuffer,
                    client->recvBuffer + payloadOffset + (size_t)payloadLength,
                    client->recvLength - (payloadOffset + (size_t)payloadLength));
            client->recvLength -= payloadOffset + (size_t)payloadLength;
        }
    }

    return true;
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
    if (clients != NULL && index >= 0 && index < RELAY_MAX_CLIENTS)
    {
        fprintf(stderr, "[relay] disconnect_client called: idx=%d reason=%s peerIndex=%d inUse=%d\n", index, reason ? reason : "(null)", clients[index].peerIndex, clients[index].inUse ? 1 : 0);
        fflush(stderr);
    }
    if (clients == NULL || index < 0 || index >= RELAY_MAX_CLIENTS || !clients[index].inUse)
    {
        return;
    }

    peerIndex = clients[index].peerIndex;
    close_socket_if_open(&clients[index].socketHandle);
    clients[index].inUse = false;
    clients[index].handshakeComplete = false;
    clients[index].paired = false;
    clients[index].transportMode = RELAY_TRANSPORT_UNKNOWN;
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
        clients[peerIndex].transportMode = RELAY_TRANSPORT_UNKNOWN;
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

    if (client->pendingLength == 0u)
    {
        return;
    }

    peer = &clients[client->peerIndex];
    if (!peer->inUse || peer->socketHandle == RELAY_INVALID_SOCKET)
    {
        return;
    }

    if (client->transportMode == RELAY_TRANSPORT_WEBSOCKET)
    {
        fprintf(stderr, "[relay] websocket relay: idx=%d -> peer=%d size=%zu\n", index, client->peerIndex, client->pendingLength);
        fflush(stderr);
        /* Send as a text frame so browser/text clients can parse easily */
        if (!websocket_frame_send(peer->socketHandle, 0x1u, client->pendingBuffer, client->pendingLength))
        {
            disconnect_client(clients, index, "send failed");
            return;
        }

        client->pendingLength = 0u;
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

    /* Diagnostic: dump client slot states for debugging pairing */
    if (clients != NULL)
    {
        fprintf(stderr, "[relay] pair_room: dump clients\n");
        for (int di = 0; di < RELAY_MAX_CLIENTS; di++)
        {
            fprintf(stderr, "  slot %d: inUse=%d handshake=%d paired=%d role=%d transport=%d room=%s\n",
                    di,
                    clients[di].inUse ? 1 : 0,
                    clients[di].handshakeComplete ? 1 : 0,
                    clients[di].paired ? 1 : 0,
                    (int)clients[di].role,
                    (int)clients[di].transportMode,
                    clients[di].inUse ? clients[di].roomCode : "-");
        }
        fflush(stderr);
    }

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

        if (clients[i].role == RELAY_ROLE_NONE || clients[i].role == client->role || clients[i].transportMode != client->transportMode)
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

    fprintf(stderr, "[relay] pairing clients: idx=%d peer=%d room=%s\n", index, peerIndex, client->roomCode);
    fflush(stderr);

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
            disconnect_client(clients, index, client->handshakeComplete ? "frame too long" : "handshake too long");
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
            if (client->recvLength >= 4u && memcmp(client->recvBuffer, "GET ", 4u) == 0)
            {
                const char *headerEnd = strstr((const char *)client->recvBuffer, "\r\n\r\n");
                if (headerEnd == NULL)
                {
                    continue;
                }

                {
                    const size_t requestLength = (size_t)(headerEnd - (const char *)client->recvBuffer) + 4u;
                    char requestText[RELAY_RECV_BUFFER_SIZE + 1u];
                    char roomCode[RELAY_MAX_ROOM_CODE + 1];
                    char acceptKey[128];
                    enum RelayRole role = RELAY_ROLE_NONE;
                    size_t leftover = client->recvLength - requestLength;

                    if (requestLength >= sizeof(requestText))
                    {
                        disconnect_client(clients, index, "websocket request too long");
                        return;
                    }

                    memcpy(requestText, client->recvBuffer, requestLength);
                    requestText[requestLength] = '\0';
                    if (!parse_websocket_request(requestText, roomCode, sizeof(roomCode), &role, acceptKey, sizeof(acceptKey)))
                    {
                        char path[128];
                        const bool havePath = try_read_http_request_path(requestText, path, sizeof(path));

                        fprintf(stderr, "[relay] rejected http request: idx=%d path=%s\n", index, havePath ? path : "(unknown)");
                        fflush(stderr);

                        if (havePath && (strcmp(path, "/") == 0 || strcmp(path, "/healthz") == 0))
                        {
                            (void)send_http_text_response(client->socketHandle,
                                                          200,
                                                          "OK",
                                                          "SoC relay is running. Use WebSocket upgrade requests with room and role query parameters.\n");
                        }
                        else
                        {
                            (void)send_http_text_response(client->socketHandle,
                                                          426,
                                                          "Upgrade Required",
                                                          "This endpoint expects a WebSocket upgrade request.\n");
                        }
                        disconnect_client(clients, index, "invalid websocket handshake");
                        return;
                    }

                    if (!send_websocket_upgrade_response(client->socketHandle, acceptKey))
                    {
                        disconnect_client(clients, index, "websocket upgrade failed");
                        return;
                    }

                    client->handshakeComplete = true;
                    client->transportMode = RELAY_TRANSPORT_WEBSOCKET;
                    client->role = role;
                    snprintf(client->roomCode, sizeof(client->roomCode), "%s", roomCode);
                    fprintf(stderr, "[relay] websocket handshake complete: idx=%d role=%s room=%s leftover=%zu\n", index, (role==RELAY_ROLE_HOST)?"HOST":"CLIENT", client->roomCode, leftover);
                    fflush(stderr);

                    if (leftover > 0u)
                    {
                        memmove(client->recvBuffer, client->recvBuffer + requestLength, leftover);
                    }
                    client->recvLength = leftover;

                    if (client->recvLength > 0u && !websocket_frame_parse(clients, index))
                    {
                        return;
                    }

                    pair_room(clients, index);
                    if (client->paired)
                    {
                        flush_client(clients, index);
                    }
                }
            }
            else
            {
                char *newline = memchr(client->recvBuffer, '\n', client->recvLength);
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
                    client->transportMode = RELAY_TRANSPORT_TCP;
                    client->role = role;
                    snprintf(client->roomCode, sizeof(client->roomCode), "%s", roomCode);
                    fprintf(stderr, "[relay] tcp handshake complete: idx=%d role=%s room=%s leftover=%zu\n", index, (role==RELAY_ROLE_HOST)?"HOST":"CLIENT", client->roomCode, leftover);
                    fflush(stderr);

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
                    if (client->paired)
                    {
                        flush_client(clients, index);
                    }
                }
            }
        }
        else if (client->transportMode == RELAY_TRANSPORT_WEBSOCKET)
        {
            if (!websocket_frame_parse(clients, index))
            {
                return;
            }

            if (client->paired)
            {
                flush_client(clients, index);
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
    fprintf(stderr, "[relay] starting main\n");
    RelaySocket listenSocket = RELAY_INVALID_SOCKET;
    struct sockaddr_in listenAddress;
    struct RelayClient *clients = NULL;
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
        fprintf(stderr, "[relay] failed to initialize socket layer\n");
        return 1;
    }

    clients = (struct RelayClient *)calloc(RELAY_MAX_CLIENTS, sizeof(*clients));
    if (clients == NULL)
    {
        fprintf(stderr, "[relay] failed to allocate clients array\n");
        close_socket_if_open(&listenSocket);
        return 1;
    }

    for (int i = 0; i < RELAY_MAX_CLIENTS; i++)
    {
        clients[i].socketHandle = RELAY_INVALID_SOCKET;
        clients[i].peerIndex = -1;
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == RELAY_INVALID_SOCKET)
    {
        fprintf(stderr, "[relay] socket failed\n");
        return 1;
    }

    {
        int reuseAddress = 1;
        setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuseAddress, (RelaySockLen)sizeof(reuseAddress));
    }

    if (!set_socket_nonblocking(listenSocket))
    {
        fprintf(stderr, "[relay] failed to set nonblocking mode\n");
        close_socket_if_open(&listenSocket);
        return 1;
    }

    memset(&listenAddress, 0, sizeof(listenAddress));
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    listenAddress.sin_port = htons(port);

    if (bind(listenSocket, (struct sockaddr *)&listenAddress, sizeof(listenAddress)) == RELAY_SOCKET_ERROR)
    {
        fprintf(stderr, "[relay] bind failed on port %u\n", (unsigned int)port);
        close_socket_if_open(&listenSocket);
        return 1;
    }

    if (listen(listenSocket, RELAY_MAX_CLIENTS) == RELAY_SOCKET_ERROR)
    {
        fprintf(stderr, "[relay] listen failed\n");
        close_socket_if_open(&listenSocket);
        return 1;
    }

    fprintf(stderr, "[relay] listening on port %u\n", (unsigned int)port);
    fflush(stderr);
    fflush(stdout);

    while (true)
    {
        /* heartbeat log to show loop alive */
        /*fprintf(stderr, "[relay] loop tick\n");*/
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
                fprintf(stderr, "[relay] accepted connection: slot=%d socket=%d\n", slot, (int)clientSocket);
                fflush(stderr);
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
    free(clients);

#ifdef _WIN32
    if (gSocketLayerReady)
    {
        WSACleanup();
        gSocketLayerReady = false;
    }
#endif

    return 0;
}
