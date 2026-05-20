#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#endif

#include "netplay.h"
#include "websocket.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
typedef SOCKET NetSocket;
typedef int NetSockLen;
#define NET_INVALID_SOCKET INVALID_SOCKET
#define NET_SOCKET_ERROR SOCKET_ERROR
#define net_close_socket closesocket
#define net_last_error_code() WSAGetLastError()
#define NET_WOULD_BLOCK(errorCode) ((errorCode) == WSAEWOULDBLOCK)
#define NET_CONNECT_IN_PROGRESS(errorCode) ((errorCode) == WSAEWOULDBLOCK || (errorCode) == WSAEINPROGRESS || (errorCode) == WSAEALREADY)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NetSocket;
typedef socklen_t NetSockLen;
#define NET_INVALID_SOCKET (-1)
#define NET_SOCKET_ERROR (-1)
#define net_close_socket close
#define net_last_error_code() errno
#define NET_WOULD_BLOCK(errorCode) ((errorCode) == EWOULDBLOCK || (errorCode) == EAGAIN)
#define NET_CONNECT_IN_PROGRESS(errorCode) ((errorCode) == EINPROGRESS || (errorCode) == EALREADY || (errorCode) == EWOULDBLOCK)
#endif

#define NETPLAY_PACKET_MAGIC "SOCN"
#define NETPLAY_SEND_QUEUE_CAPACITY 16
#define NETPLAY_EVENT_QUEUE_CAPACITY 16
#define NETPLAY_RECV_BUFFER_SIZE (NETPLAY_MAX_PAYLOAD_SIZE * 2u)
#define NETPLAY_CONNECT_TIMEOUT_MS 8000u
#define NETPLAY_HEARTBEAT_INTERVAL_MS 2000u
#define NETPLAY_STALE_TIMEOUT_MS 10000u
#define NETPLAY_RELAY_HANDSHAKE_MAX (NETPLAY_MAX_RELAY_ROOM_CODE + 32)

enum NetplayPacketType
{
    NETPLAY_PACKET_HELLO = 1,
    NETPLAY_PACKET_LOBBY_STATE = 2,
    NETPLAY_PACKET_MATCH_INIT = 3,
    NETPLAY_PACKET_SNAPSHOT = 4,
    NETPLAY_PACKET_ACTION_REQUEST = 5,
    NETPLAY_PACKET_ACTION_RESULT = 6,
    NETPLAY_PACKET_ACTION_REJECT = 7,
    NETPLAY_PACKET_TRADE_OFFER = 8,
    NETPLAY_PACKET_TRADE_RESPONSE = 9,
    NETPLAY_PACKET_CAPABILITIES = 10,
    NETPLAY_PACKET_HEARTBEAT = 11,
    NETPLAY_PACKET_RESYNC_REQUEST = 12
};

struct NetplayCapabilitiesWire
{
    uint32_t capabilityFlags;
    uint32_t protocolMinVersion;
    uint32_t protocolMaxVersion;
};

struct NetplayPacketHeader
{
    char magic[4];
    uint32_t version;
    uint32_t type;
    uint32_t payloadSize;
};

struct NetplayHelloWire
{
    uint32_t assignedPlayer;
    uint32_t hostPlayer;
    uint32_t seatAuthority[MAX_PLAYERS];
};

struct NetplayLobbyStateWire
{
    uint32_t controlMode[MAX_PLAYERS];
    uint32_t aiDifficulty[MAX_PLAYERS];
};

struct NetplayMatchInitWire
{
    uint32_t tileTypes[MAX_TILES];
    uint32_t diceNumbers[MAX_TILES];
    uint32_t developmentDeck[DEVELOPMENT_DECK_SIZE];
    uint32_t setupStartPlayer;
    uint32_t thiefTileId;
    uint32_t controlMode[MAX_PLAYERS];
    uint32_t aiDifficulty[MAX_PLAYERS];
};

struct NetplayActionWire
{
    uint32_t type;
    uint32_t tileId;
    uint32_t cornerIndex;
    uint32_t sideIndex;
    uint32_t diceRoll;
    uint32_t player;
    uint32_t resourceA;
    uint32_t resourceB;
    uint32_t amountA;
    uint32_t amountB;
    uint32_t resources[5];
};

struct NetplayActionResultWire
{
    uint32_t applied;
    uint32_t diceRoll;
    uint32_t drawnCard;
    uint32_t stolenResource;
};

struct NetplayActionOutcomeWire
{
    struct NetplayActionWire action;
    struct NetplayActionResultWire result;
    uint32_t stateHash;
};

struct NetplayRejectWire
{
    uint32_t stateHash;
    char message[NETPLAY_MAX_STATUS_TEXT];
};

struct NetplayTradeResponseWire
{
    struct NetplayActionWire action;
    uint32_t accepted;
    uint32_t stateHash;
};

struct NetplayResyncRequestWire
{
    uint32_t stateHash;
    char reason[NETPLAY_MAX_STATUS_TEXT];
};

struct NetplayQueuedPacket
{
    int peerId;
    size_t length;
    size_t offset;
    unsigned char bytes[sizeof(struct NetplayPacketHeader) + NETPLAY_MAX_PAYLOAD_SIZE];
};

struct NetplayState
{
    enum NetplayMode mode;
    enum NetplayConnectionState connectionState;
    bool relayTransport;
    NetSocket listenSocket;
    NetSocket peerSocket;
    unsigned short port;
    char peerAddress[NETPLAY_MAX_PEER_ADDRESS + 1];
    char localAddress[NETPLAY_MAX_PEER_ADDRESS + 1];
    char lastError[NETPLAY_MAX_STATUS_TEXT];
    char relayRoomCode[NETPLAY_MAX_RELAY_ROOM_CODE + 1];
    char relayHandshake[NETPLAY_RELAY_HANDSHAKE_MAX];
    size_t relayHandshakeLength;
    size_t relayHandshakeOffset;
#ifdef _WIN32
    HINTERNET relaySession;
    HINTERNET relayConnection;
    HINTERNET relayRequest;
    HINTERNET relayWebSocket;
#endif
    unsigned char recvBuffer[NETPLAY_RECV_BUFFER_SIZE];
    size_t recvLength;
    struct NetplayQueuedPacket sendQueue[NETPLAY_SEND_QUEUE_CAPACITY];
    int sendHead;
    int sendCount;
    struct NetplayEvent eventQueue[NETPLAY_EVENT_QUEUE_CAPACITY];
    int eventHead;
    int eventCount;
    uint64_t connectStartedMs;
    uint64_t lastSendMs;
    uint64_t lastReceiveMs;
    uint64_t lastHeartbeatSentMs;
    NetSocket trackedHostPeers[NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1];
    char trackedHostPeerAddress[NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1][NETPLAY_MAX_PEER_ADDRESS + 1];
    unsigned char trackedHostPeerRecvBuffer[NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1][NETPLAY_RECV_BUFFER_SIZE];
    size_t trackedHostPeerRecvLength[NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1];
    uint64_t trackedHostPeerLastReceiveMs[NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1];
};

#ifdef _WIN32
static bool gSocketLayerReady = false;
#endif

static bool net_socket_layer_init(void);
static uint64_t netplay_now_ms(void);
static bool is_loopback_host(const char *hostAddress);
static bool parse_relay_endpoint(const char *input,
                                 unsigned short fallbackPort,
                                 char *hostBuffer,
                                 size_t hostBufferSize,
                                 unsigned short *resolvedPort,
                                 bool *secureTransport);
static void detect_local_ipv4_address(char *buffer, size_t bufferSize);
static void clear_last_error(struct NetplayState *state);
static void set_last_error(struct NetplayState *state, const char *message);
static bool set_socket_nonblocking(NetSocket socketHandle);
static void set_socket_nodelay(NetSocket socketHandle);
static void close_socket_if_open(NetSocket *socketHandle);
static void close_relay_transport(struct NetplayState *state);
static bool open_relay_transport(struct NetplayState *state,
                                 const char *hostAddress,
                                 unsigned short port,
                                 bool secureTransport,
                                 bool isRelayHost,
                                 const char *roomCode);
static int send_relay_message(struct NetplayState *state, const unsigned char *buffer, size_t length, bool binaryMessage);
static int receive_relay_message(struct NetplayState *state, unsigned char *buffer, size_t bufferSize);
static NetSocket get_host_peer_socket_by_id(const struct NetplayState *state, int peerId);
static bool socket_is_writable(NetSocket socketHandle);
static void reset_connection_buffers(struct NetplayState *state);
static void clear_relay_handshake(struct NetplayState *state);
static bool prepare_relay_handshake(struct NetplayState *state, bool isHost, const char *roomCode);
static bool start_outbound_connection(struct NetplayState *state,
                                      const char *hostAddress,
                                      unsigned short port,
                                      enum NetplayMode mode,
                                      bool relayTransport,
                                      bool isRelayHost,
                                      const char *roomCode);
static int transport_send(NetSocket socketHandle, const char *buffer, int length);
static int transport_recv(NetSocket socketHandle, char *buffer, int length, int flags);
static int transport_accept(NetSocket socketHandle, struct sockaddr_in *address, NetSockLen *addressLength);
static bool push_event(struct NetplayState *state, const struct NetplayEvent *event);
static bool queue_packet_to_peer(struct NetplayState *state, int peerId, enum NetplayPacketType type, const void *payload, size_t payloadSize);
static bool queue_packet_to_all_host_peers(struct NetplayState *state, enum NetplayPacketType type, const void *payload, size_t payloadSize);
static bool queue_packet(struct NetplayState *state, enum NetplayPacketType type, const void *payload, size_t payloadSize);
static void flush_send_queue(struct NetplayState *state);
static void accept_pending_client(struct NetplayState *state);
static void advance_pending_connect(struct NetplayState *state);
static void read_from_peer(struct NetplayState *state);
static void read_from_additional_host_peers(struct NetplayState *state);
static void parse_incoming_packets(struct NetplayState *state);
static bool parse_incoming_packets_for_peer(struct NetplayState *state,
                                            int peerId,
                                            unsigned char *recvBuffer,
                                            size_t *recvLength,
                                            char *failureReason,
                                            size_t failureReasonSize);
static void process_connection_health(struct NetplayState *state);
static void poll_tracked_host_peers(struct NetplayState *state);
static void disconnect_host_peer(struct NetplayState *state, int peerId, const char *message);
static void disconnect_additional_host_peer(struct NetplayState *state, int trackedPeerIndex, const char *message);
static void disconnect_peer(struct NetplayState *state, bool keepListening, const char *message);
static bool decode_packet(struct NetplayState *state, int peerId, enum NetplayPacketType type, const unsigned char *payload, size_t payloadSize);
static void encode_lobby_state(const struct NetplayLobbyStateInfo *info, struct NetplayLobbyStateWire *wireInfo);
static void decode_lobby_state(struct NetplayLobbyStateInfo *info, const struct NetplayLobbyStateWire *wireInfo);
static void encode_match_init(const struct NetplayMatchInitInfo *info, struct NetplayMatchInitWire *wireInfo);
static void decode_match_init(struct NetplayMatchInitInfo *info, const struct NetplayMatchInitWire *wireInfo);
static void encode_action(const struct GameAction *action, struct NetplayActionWire *wireAction);
static void decode_action(struct GameAction *action, const struct NetplayActionWire *wireAction);
static void encode_action_result(const struct GameActionResult *result, struct NetplayActionResultWire *wireResult);
static void decode_action_result(struct GameActionResult *result, const struct NetplayActionResultWire *wireResult);
static bool action_is_sane(const struct GameAction *action);
static bool action_result_is_sane(const struct GameActionResult *result);
static uint32_t encode_u32(uint32_t value);
static uint32_t encode_i32(int value);
static uint32_t encode_bool(bool value);
static uint32_t decode_u32(uint32_t value);
static int decode_i32(uint32_t value);
static bool decode_bool(uint32_t value);
static void init_tracked_host_peers(struct NetplayState *state);
static void close_tracked_host_peers(struct NetplayState *state);
static int count_tracked_host_peers(const struct NetplayState *state);
static bool try_track_additional_host_peer(struct NetplayState *state, NetSocket socketHandle, const char *addressText);

static uint32_t encode_u32(uint32_t value)
{
    return htonl(value);
}

static void init_tracked_host_peers(struct NetplayState *state)
{
    if (state == NULL)
    {
        return;
    }

    for (int i = 0; i < NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1; i++)
    {
        state->trackedHostPeers[i] = NET_INVALID_SOCKET;
        state->trackedHostPeerAddress[i][0] = '\0';
        state->trackedHostPeerRecvLength[i] = 0u;
        state->trackedHostPeerLastReceiveMs[i] = 0u;
    }
}

static void close_tracked_host_peers(struct NetplayState *state)
{
    if (state == NULL)
    {
        return;
    }

    for (int i = 0; i < NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1; i++)
    {
        close_socket_if_open(&state->trackedHostPeers[i]);
        state->trackedHostPeerAddress[i][0] = '\0';
        state->trackedHostPeerRecvLength[i] = 0u;
        state->trackedHostPeerLastReceiveMs[i] = 0u;
    }
}

static int count_tracked_host_peers(const struct NetplayState *state)
{
    int count = 0;

    if (state == NULL)
    {
        return 0;
    }

    if (state->relayTransport)
    {
#ifdef _WIN32
        return state->relayWebSocket != NULL ? 1 : 0;
#else
        return 0;
#endif
    }

    if (state->peerSocket != NET_INVALID_SOCKET)
    {
        count++;
    }

    for (int i = 0; i < NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1; i++)
    {
        if (state->trackedHostPeers[i] != NET_INVALID_SOCKET)
        {
            count++;
        }
    }

    return count;
}

static bool try_track_additional_host_peer(struct NetplayState *state, NetSocket socketHandle, const char *addressText)
{
    if (state == NULL || socketHandle == NET_INVALID_SOCKET)
    {
        return false;
    }

    for (int i = 0; i < NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1; i++)
    {
        if (state->trackedHostPeers[i] == NET_INVALID_SOCKET)
        {
            struct NetplayEvent event;
            state->trackedHostPeers[i] = socketHandle;
            snprintf(state->trackedHostPeerAddress[i], sizeof(state->trackedHostPeerAddress[i]), "%s", addressText == NULL ? "" : addressText);
            state->trackedHostPeerRecvLength[i] = 0u;
            state->trackedHostPeerLastReceiveMs[i] = 0u;

            memset(&event, 0, sizeof(event));
            event.type = NETPLAY_EVENT_ADDITIONAL_CLIENT_CONNECTED;
            event.peerId = i + 1;
            snprintf(event.message, sizeof(event.message), "%s", state->trackedHostPeerAddress[i]);
            push_event(state, &event);
            return true;
        }
    }

    return false;
}

static uint32_t encode_i32(int value)
{
    return htonl((uint32_t)value);
}

static uint32_t encode_bool(bool value)
{
    return htonl(value ? 1u : 0u);
}

static uint32_t decode_u32(uint32_t value)
{
    return ntohl(value);
}

static int decode_i32(uint32_t value)
{
    return (int)ntohl(value);
}

static bool decode_bool(uint32_t value)
{
    return ntohl(value) != 0u;
}

static uint64_t netplay_now_ms(void)
{
    return (uint64_t)time(NULL) * 1000u;
}

static void encode_lobby_state(const struct NetplayLobbyStateInfo *info, struct NetplayLobbyStateWire *wireInfo)
{
    if (info == NULL || wireInfo == NULL)
    {
        return;
    }

    for (int player = 0; player < MAX_PLAYERS; player++)
    {
        wireInfo->controlMode[player] = encode_i32(info->controlMode[player]);
        wireInfo->aiDifficulty[player] = encode_i32(info->aiDifficulty[player]);
    }
}

static void decode_lobby_state(struct NetplayLobbyStateInfo *info, const struct NetplayLobbyStateWire *wireInfo)
{
    if (info == NULL || wireInfo == NULL)
    {
        return;
    }

    memset(info, 0, sizeof(*info));
    for (int player = 0; player < MAX_PLAYERS; player++)
    {
        info->controlMode[player] = (enum PlayerControlMode)decode_i32(wireInfo->controlMode[player]);
        info->aiDifficulty[player] = (enum AiDifficulty)decode_i32(wireInfo->aiDifficulty[player]);
    }
}

static void encode_match_init(const struct NetplayMatchInitInfo *info, struct NetplayMatchInitWire *wireInfo)
{
    if (info == NULL || wireInfo == NULL)
    {
        return;
    }

    memset(wireInfo, 0, sizeof(*wireInfo));
    for (int tile = 0; tile < MAX_TILES; tile++)
    {
        wireInfo->tileTypes[tile] = encode_i32(info->setup.tileTypes[tile]);
        wireInfo->diceNumbers[tile] = encode_i32(info->setup.diceNumbers[tile]);
    }
    for (int card = 0; card < DEVELOPMENT_DECK_SIZE; card++)
    {
        wireInfo->developmentDeck[card] = encode_i32(info->setup.developmentDeck[card]);
    }
    wireInfo->setupStartPlayer = encode_i32(info->setup.setupStartPlayer);
    wireInfo->thiefTileId = encode_i32(info->setup.thiefTileId);
    for (int player = 0; player < MAX_PLAYERS; player++)
    {
        wireInfo->controlMode[player] = encode_i32(info->controlMode[player]);
        wireInfo->aiDifficulty[player] = encode_i32(info->aiDifficulty[player]);
    }
}

static void decode_match_init(struct NetplayMatchInitInfo *info, const struct NetplayMatchInitWire *wireInfo)
{
    if (info == NULL || wireInfo == NULL)
    {
        return;
    }

    memset(info, 0, sizeof(*info));
    for (int tile = 0; tile < MAX_TILES; tile++)
    {
        info->setup.tileTypes[tile] = (enum TileType)decode_i32(wireInfo->tileTypes[tile]);
        info->setup.diceNumbers[tile] = decode_i32(wireInfo->diceNumbers[tile]);
    }
    for (int card = 0; card < DEVELOPMENT_DECK_SIZE; card++)
    {
        info->setup.developmentDeck[card] = (enum DevelopmentCardType)decode_i32(wireInfo->developmentDeck[card]);
    }
    info->setup.setupStartPlayer = (enum PlayerType)decode_i32(wireInfo->setupStartPlayer);
    info->setup.thiefTileId = decode_i32(wireInfo->thiefTileId);
    for (int player = 0; player < MAX_PLAYERS; player++)
    {
        info->controlMode[player] = (enum PlayerControlMode)decode_i32(wireInfo->controlMode[player]);
        info->aiDifficulty[player] = (enum AiDifficulty)decode_i32(wireInfo->aiDifficulty[player]);
    }
}

static bool net_socket_layer_init(void)
{
#ifdef _WIN32
    WSADATA data;

    if (gSocketLayerReady)
    {
        return true;
    }

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        return false;
    }

    gSocketLayerReady = true;
#endif
    return true;
}

static bool is_loopback_host(const char *hostAddress)
{
    if (hostAddress == NULL)
    {
        return false;
    }

    return strcmp(hostAddress, "127.0.0.1") == 0 ||
           strcmp(hostAddress, "localhost") == 0 ||
           strcmp(hostAddress, "::1") == 0;
}

static bool parse_relay_endpoint(const char *input,
                                 unsigned short fallbackPort,
                                 char *hostBuffer,
                                 size_t hostBufferSize,
                                 unsigned short *resolvedPort,
                                 bool *secureTransport)
{
    char buffer[256];
    const char *source = input;
    size_t length = 0u;
    char *delimiter = NULL;
    char *colon = NULL;
    bool secure = false;
    unsigned short port = fallbackPort;

    if (input == NULL || hostBuffer == NULL || hostBufferSize == 0u)
    {
        return false;
    }

    while (*source != '\0' && isspace((unsigned char)*source))
    {
        source++;
    }

    length = strlen(source);
    while (length > 0u && isspace((unsigned char)source[length - 1u]))
    {
        length--;
    }

    if (length == 0u || length >= sizeof(buffer))
    {
        return false;
    }

    memcpy(buffer, source, length);
    buffer[length] = '\0';

    if (((buffer[0] == 'w' || buffer[0] == 'W') &&
         (buffer[1] == 's' || buffer[1] == 'S') &&
         buffer[2] == ':' &&
         buffer[3] == '/' &&
         buffer[4] == '/'))
    {
        memmove(buffer, buffer + 5, strlen(buffer + 5) + 1u);
    }
    else if (((buffer[0] == 'w' || buffer[0] == 'W') &&
              (buffer[1] == 's' || buffer[1] == 'S') &&
              (buffer[2] == 's' || buffer[2] == 'S') &&
              buffer[3] == ':' &&
              buffer[4] == '/' &&
              buffer[5] == '/'))
    {
        secure = true;
        memmove(buffer, buffer + 6, strlen(buffer + 6) + 1u);
    }
    else if (((buffer[0] == 'h' || buffer[0] == 'H') &&
              (buffer[1] == 't' || buffer[1] == 'T') &&
              (buffer[2] == 't' || buffer[2] == 'T') &&
              (buffer[3] == 'p' || buffer[3] == 'P') &&
              buffer[4] == ':' &&
              buffer[5] == '/' &&
              buffer[6] == '/'))
    {
        memmove(buffer, buffer + 7, strlen(buffer + 7) + 1u);
    }
    else if (((buffer[0] == 'h' || buffer[0] == 'H') &&
              (buffer[1] == 't' || buffer[1] == 'T') &&
              (buffer[2] == 't' || buffer[2] == 'T') &&
              (buffer[3] == 'p' || buffer[3] == 'P') &&
              (buffer[4] == 's' || buffer[4] == 'S') &&
              buffer[5] == ':' &&
              buffer[6] == '/' &&
              buffer[7] == '/'))
    {
        secure = true;
        memmove(buffer, buffer + 8, strlen(buffer + 8) + 1u);
    }

    while (buffer[0] == '/' && buffer[1] == '/')
    {
        memmove(buffer, buffer + 2, strlen(buffer + 2) + 1u);
    }

    delimiter = strpbrk(buffer, "/?#");
    if (delimiter != NULL)
    {
        *delimiter = '\0';
    }

    colon = strrchr(buffer, ':');
    if (colon != NULL && strchr(buffer, '[') == NULL && strchr(colon + 1, ':') == NULL)
    {
        char *portEnd = NULL;
        const long parsedPort = strtol(colon + 1, &portEnd, 10);
        if (colon[1] != '\0' &&
            portEnd != NULL &&
            *portEnd == '\0' &&
            parsedPort > 0L &&
            parsedPort <= 65535L)
        {
            *colon = '\0';
            port = (unsigned short)parsedPort;
        }
    }

    if (buffer[0] == '\0' || strlen(buffer) >= hostBufferSize)
    {
        return false;
    }

    if (secure && port == NETPLAY_DEFAULT_PORT)
    {
        port = 443u;
    }

    snprintf(hostBuffer, hostBufferSize, "%s", buffer);
    if (resolvedPort != NULL)
    {
        *resolvedPort = port;
    }
    if (secureTransport != NULL)
    {
        *secureTransport = secure;
    }
    return true;
}

static void detect_local_ipv4_address(char *buffer, size_t bufferSize)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *current = NULL;
    char hostName[256];

    if (buffer == NULL || bufferSize == 0u)
    {
        return;
    }

    snprintf(buffer, bufferSize, "%s", "127.0.0.1");
    if (gethostname(hostName, (int)sizeof(hostName)) != 0)
    {
        return;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostName, NULL, &hints, &results) != 0 || results == NULL)
    {
        return;
    }

    for (current = results; current != NULL; current = current->ai_next)
    {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)current->ai_addr;
        const char *addressText = inet_ntoa(ipv4->sin_addr);

        if (addressText == NULL)
        {
            continue;
        }

        if (strncmp(addressText, "127.", 4) == 0)
        {
            continue;
        }

        snprintf(buffer, bufferSize, "%s", addressText);
        break;
    }

    freeaddrinfo(results);
}

static void clear_last_error(struct NetplayState *state)
{
    if (state == NULL)
    {
        return;
    }

    state->lastError[0] = '\0';
}

static void set_last_error(struct NetplayState *state, const char *message)
{
    if (state == NULL)
    {
        return;
    }

    snprintf(state->lastError, sizeof(state->lastError), "%s", message == NULL ? "" : message);
}

static bool set_socket_nonblocking(NetSocket socketHandle)
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

static void set_socket_nodelay(NetSocket socketHandle)
{
    int enabled = 1;
    setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled, (NetSockLen)sizeof(enabled));
}

static void close_socket_if_open(NetSocket *socketHandle)
{
    if (socketHandle == NULL || *socketHandle == NET_INVALID_SOCKET)
    {
        return;
    }

    net_close_socket(*socketHandle);
    *socketHandle = NET_INVALID_SOCKET;
}

static void close_relay_transport(struct NetplayState *state)
{
#ifdef _WIN32
    if (state == NULL)
    {
        return;
    }

    if (state->relayWebSocket != NULL)
    {
        WinHttpWebSocketClose(state->relayWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        WinHttpCloseHandle(state->relayWebSocket);
        state->relayWebSocket = NULL;
    }

    if (state->relayRequest != NULL)
    {
        WinHttpCloseHandle(state->relayRequest);
        state->relayRequest = NULL;
    }

    if (state->relayConnection != NULL)
    {
        WinHttpCloseHandle(state->relayConnection);
        state->relayConnection = NULL;
    }

    if (state->relaySession != NULL)
    {
        WinHttpCloseHandle(state->relaySession);
        state->relaySession = NULL;
    }
#else
    (void)state;
#endif
}

static bool open_relay_transport(struct NetplayState *state,
                                 const char *hostAddress,
                                 unsigned short port,
                                 bool secureTransport,
                                 bool isRelayHost,
                                 const char *roomCode)
{
#ifdef _WIN32
    char requestPath[128];
    wchar_t wideHost[256];
    wchar_t widePath[256];
    DWORD statusCode = 0;
    DWORD statusSize = (DWORD)sizeof(statusCode);
    const char *roleText = isRelayHost ? "HOST" : "CLIENT";
    const DWORD requestFlags = secureTransport ? WINHTTP_FLAG_SECURE : 0;

    if (state == NULL || hostAddress == NULL || hostAddress[0] == '\0')
    {
        return false;
    }

    close_relay_transport(state);

    if (MultiByteToWideChar(CP_UTF8, 0, "SoC Relay", -1, widePath, (int)(sizeof(widePath) / sizeof(widePath[0]))) == 0)
    {
        set_last_error(state, "websocket session open failed");
        return false;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, hostAddress, -1, wideHost, (int)(sizeof(wideHost) / sizeof(wideHost[0]))) == 0)
    {
        set_last_error(state, "websocket host conversion failed");
        return false;
    }

    state->relaySession = WinHttpOpen(widePath, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (state->relaySession == NULL)
    {
        set_last_error(state, "websocket session open failed");
        return false;
    }

    if (!WinHttpSetTimeouts(state->relaySession, 5000, 5000, 5000, 1))
    {
        set_last_error(state, "websocket timeout setup failed");
        close_relay_transport(state);
        return false;
    }

    state->relayConnection = WinHttpConnect(state->relaySession, wideHost, port, 0);
    if (state->relayConnection == NULL)
    {
        set_last_error(state, "websocket connect failed");
        close_relay_transport(state);
        return false;
    }

    snprintf(requestPath, sizeof(requestPath), "/?room=%s&role=%s", roomCode != NULL && roomCode[0] != '\0' ? roomCode : "default", roleText);
    if (MultiByteToWideChar(CP_UTF8, 0, requestPath, -1, widePath, (int)(sizeof(widePath) / sizeof(widePath[0]))) == 0)
    {
        set_last_error(state, "websocket path conversion failed");
        close_relay_transport(state);
        return false;
    }

    state->relayRequest = WinHttpOpenRequest(state->relayConnection,
                                             L"GET",
                                             widePath,
                                             NULL,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             requestFlags);
    if (state->relayRequest == NULL)
    {
        set_last_error(state, "websocket request failed");
        close_relay_transport(state);
        return false;
    }

    if (!WinHttpSetOption(state->relayRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0))
    {
        set_last_error(state, "websocket upgrade option failed");
        close_relay_transport(state);
        return false;
    }

    if (!WinHttpSendRequest(state->relayRequest,
                            WINHTTP_NO_ADDITIONAL_HEADERS,
                            0,
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            0,
                            0) ||
        !WinHttpReceiveResponse(state->relayRequest, NULL))
    {
        set_last_error(state, "websocket handshake failed");
        close_relay_transport(state);
        return false;
    }

    if (!WinHttpQueryHeaders(state->relayRequest,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &statusCode,
                             &statusSize,
                             WINHTTP_NO_HEADER_INDEX) ||
        statusCode != 101u)
    {
        set_last_error(state, "websocket upgrade rejected");
        close_relay_transport(state);
        return false;
    }

    state->relayWebSocket = WinHttpWebSocketCompleteUpgrade(state->relayRequest, 0);
    if (state->relayWebSocket == NULL)
    {
        set_last_error(state, "websocket upgrade complete failed");
        close_relay_transport(state);
        return false;
    }

    WinHttpCloseHandle(state->relayRequest);
    state->relayRequest = NULL;
    clear_last_error(state);
    return true;
#else
    int sock = NET_INVALID_SOCKET;
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *cur = NULL;
    char portText[16];
    int lookup = 0;
    char requestPath[128];
    char keyBase64[64];
    unsigned char keyRaw[16];

    if (state == NULL || hostAddress == NULL || hostAddress[0] == '\0')
    {
        return false;
    }

    if (secureTransport)
    {
        set_last_error(state, "secure relay transport is supported only on Windows builds");
        return false;
    }

    close_relay_transport(state);

    snprintf(requestPath, sizeof(requestPath), "/?room=%s&role=%s", roomCode != NULL && roomCode[0] != '\0' ? roomCode : "default", isRelayHost ? "HOST" : "CLIENT");

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(portText, sizeof(portText), "%hu", port);
    lookup = getaddrinfo(hostAddress, portText, &hints, &results);
    if (lookup != 0 || results == NULL)
    {
        set_last_error(state, "relay lookup failed");
        return false;
    }

    for (cur = results; cur != NULL; cur = cur->ai_next)
    {
        sock = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sock == NET_INVALID_SOCKET)
        {
            continue;
        }

        if (connect(sock, cur->ai_addr, (NetSockLen)cur->ai_addrlen) != 0)
        {
            net_close_socket(sock);
            sock = NET_INVALID_SOCKET;
            continue;
        }

        break;
    }

    freeaddrinfo(results);

    if (sock == NET_INVALID_SOCKET)
    {
        set_last_error(state, "relay connect failed");
        return false;
    }

    set_socket_nodelay(sock);

    /* generate a random Sec-WebSocket-Key (base64 of 16 bytes) */
    {
        FILE *ur = fopen("/dev/urandom", "rb");
        if (ur != NULL)
        {
            fread(keyRaw, 1, sizeof(keyRaw), ur);
            fclose(ur);
        }
        else
        {
            for (size_t i = 0; i < sizeof(keyRaw); i++)
            {
                keyRaw[i] = (unsigned char)(rand() & 0xff);
            }
        }
        /* simple base64 encode */
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int v = 0, val = 0, idx = 0;
        for (size_t i = 0; i < sizeof(keyRaw); i++)
        {
            val = (val << 8) | keyRaw[i];
            v += 8;
            while (v >= 6)
            {
                keyBase64[idx++] = b64[(val >> (v - 6)) & 0x3f];
                v -= 6;
            }
        }
        if (v > 0)
        {
            keyBase64[idx++] = b64[(val << (6 - v)) & 0x3f];
        }
        while (idx % 4 != 0)
        {
            keyBase64[idx++] = '=';
        }
        keyBase64[idx] = '\0';
    }

    {
        char request[512];
        int written = snprintf(request, sizeof(request),
                               "GET %s HTTP/1.1\r\n"
                               "Host: %s:%hu\r\n"
                               "Upgrade: websocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Sec-WebSocket-Version: 13\r\n"
                               "Sec-WebSocket-Key: %s\r\n"
                               "\r\n",
                               requestPath, hostAddress, port, keyBase64);

        if (written <= 0 || (size_t)written >= sizeof(request))
        {
            net_close_socket(sock);
            set_last_error(state, "relay handshake build failed");
            return false;
        }

        if (send(sock, request, written, 0) != written)
        {
            net_close_socket(sock);
            set_last_error(state, "relay handshake send failed");
            return false;
        }

        /* receive response headers */
        {
            char response[2048];
            size_t received = 0;
            while (received + 1 < sizeof(response))
            {
                int r = recv(sock, response + received, (int)(sizeof(response) - received - 1), 0);
                if (r == 0)
                {
                    net_close_socket(sock);
                    set_last_error(state, "relay handshake closed");
                    return false;
                }
                if (r < 0)
                {
                    net_close_socket(sock);
                    set_last_error(state, "relay handshake recv failed");
                    return false;
                }
                received += (size_t)r;
                response[received] = '\0';
                if (strstr(response, "\r\n\r\n") != NULL)
                {
                    break;
                }
            }

            /* check status line */
            if (strncmp(response, "HTTP/1.1 101", 12) != 0 && strncmp(response, "HTTP/1.0 101", 12) != 0)
            {
                net_close_socket(sock);
                set_last_error(state, "relay upgrade rejected");
                return false;
            }

            /* verify Sec-WebSocket-Accept matches our key */
            {
                char expectedAccept[128];
                char *acceptHeader = NULL;
                char *line = response;
                size_t lineLen = 0;

                if (!websocket_accept_key(keyBase64, expectedAccept, sizeof(expectedAccept)))
                {
                    net_close_socket(sock);
                    set_last_error(state, "relay accept key compute failed");
                    return false;
                }

                /* find header line */
                acceptHeader = strstr(response, "Sec-WebSocket-Accept:");
                if (acceptHeader == NULL)
                {
                    net_close_socket(sock);
                    set_last_error(state, "relay missing accept header");
                    return false;
                }

                acceptHeader += strlen("Sec-WebSocket-Accept:");
                while (*acceptHeader == ' ' || *acceptHeader == '\t') acceptHeader++;
                line = acceptHeader;
                while (*line != '\0' && *line != '\r' && *line != '\n') line++;
                lineLen = (size_t)(line - acceptHeader);
                if (lineLen == 0 || lineLen >= sizeof(expectedAccept))
                {
                    net_close_socket(sock);
                    set_last_error(state, "relay invalid accept header");
                    return false;
                }

                char acceptValue[128];
                memcpy(acceptValue, acceptHeader, lineLen);
                acceptValue[lineLen] = '\0';
                if (strcmp(acceptValue, expectedAccept) != 0)
                {
                    net_close_socket(sock);
                    set_last_error(state, "relay accept mismatch");
                    return false;
                }
            }
        }
    }

    /* set non-blocking for normal operation */
    if (!set_socket_nonblocking(sock))
    {
        net_close_socket(sock);
        set_last_error(state, "relay set nonblocking failed");
        return false;
    }

    state->peerSocket = sock;
    clear_last_error(state);
    return true;
#endif
}

static int send_relay_message(struct NetplayState *state, const unsigned char *buffer, size_t length, bool binaryMessage)
{
#ifdef _WIN32
    DWORD result = 0;

    if (state == NULL || state->relayWebSocket == NULL)
    {
        return NET_SOCKET_ERROR;
    }

    result = WinHttpWebSocketSend(state->relayWebSocket,
                                  binaryMessage ? WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                  (PVOID)buffer,
                                  (DWORD)length);
    if (result != NO_ERROR)
    {
        return NET_SOCKET_ERROR;
    }

    return (int)length;
#else
    if (state == NULL || buffer == NULL || length == 0u || state->peerSocket == NET_INVALID_SOCKET)
    {
        return NET_SOCKET_ERROR;
    }

    /* Build a single-frame unfragmented client-to-server masked frame (clients MUST mask) */
    {
        unsigned char header[14];
        size_t headerLen = 0;
        uint64_t payloadLen = length;

        header[0] = binaryMessage ? 0x82u : 0x81u; /* FIN + opcode */

        if (payloadLen <= 125u)
        {
            header[1] = 0x80u | (unsigned char)payloadLen; /* MASK bit set */
            headerLen = 2;
        }
        else if (payloadLen <= 0xFFFFu)
        {
            header[1] = 0x80u | 126u;
            header[2] = (unsigned char)((payloadLen >> 8) & 0xFFu);
            header[3] = (unsigned char)(payloadLen & 0xFFu);
            headerLen = 4;
        }
        else
        {
            header[1] = 0x80u | 127u;
            /* network byte order 64-bit length */
            for (int i = 0; i < 8; i++)
            {
                header[2 + i] = (unsigned char)((payloadLen >> (8 * (7 - i))) & 0xFFu);
            }
            headerLen = 10;
        }

        /* generate mask key */
        unsigned char maskKey[4];
        FILE *ur = fopen("/dev/urandom", "rb");
        if (ur != NULL)
        {
            fread(maskKey, 1, 4, ur);
            fclose(ur);
        }
        else
        {
            for (int i = 0; i < 4; i++)
            {
                maskKey[i] = (unsigned char)(rand() & 0xFF);
            }
        }

        /* append mask key */
        for (int i = 0; i < 4; i++)
        {
            header[headerLen + i] = maskKey[i];
        }
        headerLen += 4;

        /* prepare masked payload in a temporary buffer if necessary */
        unsigned char *sendBuffer = NULL;
        size_t sendSize = headerLen + payloadLen;
        sendBuffer = (unsigned char *)malloc(sendSize);
        if (sendBuffer == NULL)
        {
            return NET_SOCKET_ERROR;
        }

        memcpy(sendBuffer, header, headerLen);
        for (size_t i = 0; i < payloadLen; i++)
        {
            sendBuffer[headerLen + i] = ((unsigned char *)buffer)[i] ^ maskKey[i & 3];
        }

        /* Temporarily clear non-blocking so send_all_socket can block until complete */
        /* Get flags */
#ifdef _WIN32
        (void)0;
#else
        int flags = fcntl(state->peerSocket, F_GETFL, 0);
        if (flags >= 0)
        {
            fcntl(state->peerSocket, F_SETFL, flags & ~O_NONBLOCK);
        }
#endif

        /* send all */
        size_t sentTotal = 0u;
        while (sentTotal < sendSize)
        {
            int s = send(state->peerSocket, (const char *)(sendBuffer + sentTotal), (int)(sendSize - sentTotal), 0);
            if (s == NET_SOCKET_ERROR)
            {
                free(sendBuffer);
                return NET_SOCKET_ERROR;
            }
            if (s <= 0)
            {
                free(sendBuffer);
                return NET_SOCKET_ERROR;
            }
            sentTotal += (size_t)s;
        }

#ifdef _WIN32
        (void)0;
#else
        /* restore flags */
        if (flags >= 0)
        {
            fcntl(state->peerSocket, F_SETFL, flags);
        }
#endif

        free(sendBuffer);
        return (int)length;
    }
#endif
}

static int receive_relay_message(struct NetplayState *state, unsigned char *buffer, size_t bufferSize)
{
#ifdef _WIN32
    WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
    DWORD bytesRead = 0;
    size_t totalBytes = 0u;

    if (state == NULL || buffer == NULL || bufferSize == 0u || state->relayWebSocket == NULL)
    {
        return NET_SOCKET_ERROR;
    }

    do
    {
        bytesRead = 0u;
        bufferType = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        if (WinHttpWebSocketReceive(state->relayWebSocket,
                                    buffer + totalBytes,
                                    (DWORD)(bufferSize - totalBytes),
                                    &bytesRead,
                                    &bufferType) != NO_ERROR)
        {
            if (GetLastError() == ERROR_WINHTTP_TIMEOUT)
            {
                return -2;
            }
            return NET_SOCKET_ERROR;
        }

        if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
        {
            return 0;
        }

        totalBytes += (size_t)bytesRead;
        if (totalBytes > bufferSize)
        {
            return NET_SOCKET_ERROR;
        }
    }
    while (bufferType == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE ||
           bufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE);

    return (int)totalBytes;
#else
    if (state == NULL || buffer == NULL || bufferSize == 0u || state->peerSocket == NET_INVALID_SOCKET)
    {
        return NET_SOCKET_ERROR;
    }

    /* Peek header (2 bytes) */
    unsigned char hdr[2];
    int r = recv(state->peerSocket, (char *)hdr, 2, MSG_PEEK);
    if (r == 0)
    {
        return 0; /* closed */
    }
    if (r < 0)
    {
        const int err = net_last_error_code();
        if (NET_WOULD_BLOCK(err))
        {
            return -2;
        }
        return NET_SOCKET_ERROR;
    }
    if (r < 2)
    {
        return -2;
    }

    const unsigned char b0 = hdr[0];
    const unsigned char b1 = hdr[1];
    const bool fin = (b0 & 0x80u) != 0;
    const unsigned char opcode = b0 & 0x0Fu;
    const bool masked = (b1 & 0x80u) != 0;
    uint64_t payloadLen = b1 & 0x7Fu;
    size_t headerLen = 2;

    if (payloadLen == 126u)
    {
        unsigned char ext[2];
        r = recv(state->peerSocket, (char *)ext, 2, MSG_PEEK | MSG_WAITALL);
        if (r < 2)
        {
            return -2;
        }
        payloadLen = ((uint64_t)ext[0] << 8) | (uint64_t)ext[1];
        headerLen += 2;
    }
    else if (payloadLen == 127u)
    {
        unsigned char ext[8];
        r = recv(state->peerSocket, (char *)ext, 8, MSG_PEEK | MSG_WAITALL);
        if (r < 8)
        {
            return -2;
        }
        payloadLen = 0;
        for (int i = 0; i < 8; i++)
        {
            payloadLen = (payloadLen << 8) | ext[i];
        }
        headerLen += 8;
    }

    if (masked)
    {
        headerLen += 4;
    }

    /* Ensure we have the whole frame available */
    size_t bytesNeeded = headerLen + (size_t)payloadLen;
    /* Peek available bytes */
    {
        /* Use ioctl(FIONREAD) to check available bytes */
        int available = 0;
#ifdef _WIN32
        unsigned long avail = 0;
        if (ioctlsocket(state->peerSocket, FIONREAD, &avail) != 0)
        {
            return NET_SOCKET_ERROR;
        }
        available = (int)avail;
#else
        if (ioctl(state->peerSocket, FIONREAD, &available) != 0)
        {
            const int err = net_last_error_code();
            if (NET_WOULD_BLOCK(err))
            {
                return -2;
            }
            return NET_SOCKET_ERROR;
        }
#endif
        if ((size_t)available < bytesNeeded)
        {
            return -2;
        }
    }

    /* Read full frame into temp buffer */
    size_t toRead = bytesNeeded;
    unsigned char *tmp = (unsigned char *)malloc(toRead);
    if (tmp == NULL)
    {
        return NET_SOCKET_ERROR;
    }
    size_t readTotal = 0;
    while (readTotal < toRead)
    {
        int rr = recv(state->peerSocket, (char *)(tmp + readTotal), (int)(toRead - readTotal), 0);
        if (rr == 0)
        {
            free(tmp);
            return 0;
        }
        if (rr < 0)
        {
            const int err = net_last_error_code();
            if (NET_WOULD_BLOCK(err))
            {
                free(tmp);
                return -2;
            }
            free(tmp);
            return NET_SOCKET_ERROR;
        }
        readTotal += (size_t)rr;
    }

    /* parse payload from tmp */
    size_t offset = 2;
    if ((b1 & 0x7Fu) == 126u)
    {
        offset += 2;
    }
    else if ((b1 & 0x7Fu) == 127u)
    {
        offset += 8;
    }

    unsigned char maskKey[4] = {0, 0, 0, 0};
    if (masked)
    {
        for (int i = 0; i < 4; i++)
        {
            maskKey[i] = tmp[offset + i];
        }
        offset += 4;
    }

    if (payloadLen > bufferSize)
    {
        free(tmp);
        return NET_SOCKET_ERROR;
    }

    for (uint64_t i = 0; i < payloadLen; i++)
    {
        unsigned char val = tmp[offset + (size_t)i];
        if (masked)
        {
            val ^= maskKey[i & 3];
        }
        buffer[i] = val;
    }

    free(tmp);
    return (int)payloadLen;
#endif
}

static NetSocket get_host_peer_socket_by_id(const struct NetplayState *state, int peerId)
{
    if (state == NULL || peerId < 0 || peerId >= NETPLAY_MAX_HOST_REMOTE_PLAYERS)
    {
        return NET_INVALID_SOCKET;
    }

    if (peerId == 0)
    {
        return state->peerSocket;
    }

    return state->trackedHostPeers[peerId - 1];
}

static bool socket_is_writable(NetSocket socketHandle)
{
    fd_set writeSet;
    struct timeval timeout;

    FD_ZERO(&writeSet);
    FD_SET(socketHandle, &writeSet);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select((int)socketHandle + 1, NULL, &writeSet, NULL, &timeout) > 0 && FD_ISSET(socketHandle, &writeSet);
}

static void reset_connection_buffers(struct NetplayState *state)
{
    if (state == NULL)
    {
        return;
    }

    state->recvLength = 0u;
    state->sendHead = 0;
    state->sendCount = 0;
    state->lastSendMs = 0u;
    state->lastReceiveMs = 0u;
    state->lastHeartbeatSentMs = 0u;

    for (int i = 0; i < NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1; i++)
    {
        state->trackedHostPeerRecvLength[i] = 0u;
        state->trackedHostPeerLastReceiveMs[i] = 0u;
    }
}

static void clear_relay_handshake(struct NetplayState *state)
{
    if (state == NULL)
    {
        return;
    }

    state->relayHandshake[0] = '\0';
    state->relayHandshakeLength = 0u;
    state->relayHandshakeOffset = 0u;
}

static int transport_send(NetSocket socketHandle, const char *buffer, int length)
{
    return send(socketHandle, buffer, length, 0);
}

static int transport_recv(NetSocket socketHandle, char *buffer, int length, int flags)
{
    return recv(socketHandle, buffer, length, flags);
}

static int transport_accept(NetSocket socketHandle, struct sockaddr_in *address, NetSockLen *addressLength)
{
    return accept(socketHandle, (struct sockaddr *)address, addressLength);
}

static bool prepare_relay_handshake(struct NetplayState *state, bool isHost, const char *roomCode)
{
    const char *normalizedRoom = NULL;

    if (state == NULL)
    {
        return false;
    }

    if (roomCode != NULL && roomCode[0] != '\0')
    {
        snprintf(state->relayRoomCode, sizeof(state->relayRoomCode), "%s", roomCode);
    }

    normalizedRoom = state->relayRoomCode[0] != '\0' ? state->relayRoomCode : "default";

    snprintf(state->relayHandshake,
             sizeof(state->relayHandshake),
             "SOC-RELAY 1 %s %s\n",
             normalizedRoom,
             isHost ? "HOST" : "CLIENT");
    state->relayHandshakeLength = strlen(state->relayHandshake);
    state->relayHandshakeOffset = 0u;
    return true;
}

static bool push_event(struct NetplayState *state, const struct NetplayEvent *event)
{
    if (state == NULL || event == NULL || state->eventCount >= NETPLAY_EVENT_QUEUE_CAPACITY)
    {
        return false;
    }

    {
        const int writeIndex = (state->eventHead + state->eventCount) % NETPLAY_EVENT_QUEUE_CAPACITY;
        state->eventQueue[writeIndex] = *event;
        state->eventCount++;
    }

    return true;
}

static bool queue_packet_to_peer(struct NetplayState *state,
                                 int peerId,
                                 enum NetplayPacketType type,
                                 const void *payload,
                                 size_t payloadSize)
{
    struct NetplayQueuedPacket *packet = NULL;
    struct NetplayPacketHeader header;
    NetSocket peerSocket = NET_INVALID_SOCKET;
    bool relayPeerAvailable = false;

    if (state == NULL || payloadSize > NETPLAY_MAX_PAYLOAD_SIZE || peerId < 0)
    {
        return false;
    }

    if (state->connectionState != NETPLAY_CONNECTION_CONNECTED)
    {
        set_last_error(state, "queue failed: not connected");
        return false;
    }

    if (state->relayTransport)
    {
#ifdef _WIN32
        relayPeerAvailable = (peerId == 0 && state->relayWebSocket != NULL);
#else
        relayPeerAvailable = false;
#endif
    }
    else if (state->mode == NETPLAY_MODE_HOST)
    {
        peerSocket = get_host_peer_socket_by_id(state, peerId);
    }
    else
    {
        peerSocket = (peerId == 0) ? state->peerSocket : NET_INVALID_SOCKET;
    }

    if (!relayPeerAvailable && peerSocket == NET_INVALID_SOCKET)
    {
        set_last_error(state, "queue failed: peer unavailable");
        return false;
    }

    if (state->sendCount >= NETPLAY_SEND_QUEUE_CAPACITY)
    {
        set_last_error(state, "queue failed: send queue full");
        return false;
    }

    {
        const int writeIndex = (state->sendHead + state->sendCount) % NETPLAY_SEND_QUEUE_CAPACITY;
        packet = &state->sendQueue[writeIndex];
        packet->peerId = peerId;
        header.magic[0] = NETPLAY_PACKET_MAGIC[0];
        header.magic[1] = NETPLAY_PACKET_MAGIC[1];
        header.magic[2] = NETPLAY_PACKET_MAGIC[2];
        header.magic[3] = NETPLAY_PACKET_MAGIC[3];
        header.version = encode_u32(NETPLAY_PROTOCOL_VERSION);
        header.type = encode_u32((uint32_t)type);
        header.payloadSize = encode_u32((uint32_t)payloadSize);
        memcpy(packet->bytes, &header, sizeof(header));
        if (payloadSize > 0u && payload != NULL)
        {
            memcpy(packet->bytes + sizeof(header), payload, payloadSize);
        }
        packet->length = sizeof(header) + payloadSize;
        packet->offset = 0u;
        state->sendCount++;
    }

    return true;
}

static bool queue_packet_to_all_host_peers(struct NetplayState *state,
                                           enum NetplayPacketType type,
                                           const void *payload,
                                           size_t payloadSize)
{
    bool queuedAtLeastOne = false;

    if (state == NULL)
    {
        return false;
    }

    if (state->relayTransport)
    {
        return queue_packet_to_peer(state, 0, type, payload, payloadSize);
    }

    for (int peerId = 0; peerId < NETPLAY_MAX_HOST_REMOTE_PLAYERS; peerId++)
    {
        if (get_host_peer_socket_by_id(state, peerId) == NET_INVALID_SOCKET)
        {
            continue;
        }

        if (!queue_packet_to_peer(state, peerId, type, payload, payloadSize))
        {
            return false;
        }
        queuedAtLeastOne = true;
    }

    return queuedAtLeastOne;
}

static bool queue_packet(struct NetplayState *state, enum NetplayPacketType type, const void *payload, size_t payloadSize)
{
    return queue_packet_to_peer(state, 0, type, payload, payloadSize);
}

static void disconnect_peer(struct NetplayState *state, bool keepListening, const char *message)
{
    struct NetplayEvent event;

    if (state == NULL)
    {
        return;
    }

    close_socket_if_open(&state->peerSocket);
    close_tracked_host_peers(state);
    close_relay_transport(state);
    reset_connection_buffers(state);
    clear_relay_handshake(state);
    state->connectStartedMs = 0u;
    state->peerAddress[0] = '\0';
    if (keepListening && state->mode == NETPLAY_MODE_HOST && state->listenSocket != NET_INVALID_SOCKET)
    {
        state->connectionState = NETPLAY_CONNECTION_LISTENING;
    }
    else
    {
        state->connectionState = NETPLAY_CONNECTION_DISCONNECTED;
    }

    memset(&event, 0, sizeof(event));
    event.type = NETPLAY_EVENT_DISCONNECTED;
    snprintf(event.message, sizeof(event.message), "%s", message == NULL ? "" : message);
    push_event(state, &event);
}

static void disconnect_additional_host_peer(struct NetplayState *state, int trackedPeerIndex, const char *message)
{
    struct NetplayEvent event;

    if (state == NULL || trackedPeerIndex < 0 || trackedPeerIndex >= NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1)
    {
        return;
    }

    if (state->trackedHostPeers[trackedPeerIndex] == NET_INVALID_SOCKET)
    {
        return;
    }

    close_socket_if_open(&state->trackedHostPeers[trackedPeerIndex]);
    memset(&event, 0, sizeof(event));
    event.type = NETPLAY_EVENT_ADDITIONAL_CLIENT_DISCONNECTED;
    event.peerId = trackedPeerIndex + 1;
    snprintf(event.message,
             sizeof(event.message),
             "%s",
             (message != NULL && message[0] != '\0') ? message : state->trackedHostPeerAddress[trackedPeerIndex]);
    state->trackedHostPeerAddress[trackedPeerIndex][0] = '\0';
    state->trackedHostPeerRecvLength[trackedPeerIndex] = 0u;
    state->trackedHostPeerLastReceiveMs[trackedPeerIndex] = 0u;
    push_event(state, &event);
}

static void disconnect_host_peer(struct NetplayState *state, int peerId, const char *message)
{
    struct NetplayEvent event;
    char peerAddress[NETPLAY_MAX_PEER_ADDRESS + 1];
    const bool disconnectAll = state != NULL && count_tracked_host_peers(state) <= 1;

    if (state == NULL || state->mode != NETPLAY_MODE_HOST || peerId < 0)
    {
        return;
    }

    if (state->relayTransport)
    {
        disconnect_peer(state, false, message);
        return;
    }

    peerAddress[0] = '\0';
    if (peerId == 0)
    {
        snprintf(peerAddress, sizeof(peerAddress), "%s", state->peerAddress);
        close_socket_if_open(&state->peerSocket);
        state->peerAddress[0] = '\0';
        state->recvLength = 0u;
        state->lastReceiveMs = 0u;
    }
    else if (peerId <= NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1)
    {
        const int trackedPeerIndex = peerId - 1;
        snprintf(peerAddress, sizeof(peerAddress), "%s", state->trackedHostPeerAddress[trackedPeerIndex]);
        close_socket_if_open(&state->trackedHostPeers[trackedPeerIndex]);
        state->trackedHostPeerAddress[trackedPeerIndex][0] = '\0';
        state->trackedHostPeerRecvLength[trackedPeerIndex] = 0u;
        state->trackedHostPeerLastReceiveMs[trackedPeerIndex] = 0u;
    }
    else
    {
        return;
    }

    if (!disconnectAll)
    {
        memset(&event, 0, sizeof(event));
        event.type = NETPLAY_EVENT_ADDITIONAL_CLIENT_DISCONNECTED;
        event.peerId = peerId;
        snprintf(event.message,
                 sizeof(event.message),
                 "%s",
                 (message != NULL && message[0] != '\0') ? message : peerAddress);
        push_event(state, &event);
        return;
    }

    reset_connection_buffers(state);
    state->connectStartedMs = 0u;
    state->connectionState = state->listenSocket != NET_INVALID_SOCKET
                                 ? NETPLAY_CONNECTION_LISTENING
                                 : NETPLAY_CONNECTION_DISCONNECTED;

    memset(&event, 0, sizeof(event));
    event.type = NETPLAY_EVENT_DISCONNECTED;
    snprintf(event.message,
             sizeof(event.message),
             "%s",
             (message != NULL && message[0] != '\0') ? message : "remote player disconnected");
    push_event(state, &event);
}

static void poll_tracked_host_peers(struct NetplayState *state)
{
    if (state == NULL || state->mode != NETPLAY_MODE_HOST || state->relayTransport)
    {
        return;
    }

    for (int i = 0; i < NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1; i++)
    {
        if (state->trackedHostPeers[i] != NET_INVALID_SOCKET)
        {
            unsigned char peekByte = 0u;
            const int received = transport_recv(state->trackedHostPeers[i], (char *)&peekByte, 1, MSG_PEEK);
            if (received == 0)
            {
                disconnect_additional_host_peer(state, i, NULL);
            }
            else if (received == NET_SOCKET_ERROR)
            {
                const int errorCode = net_last_error_code();
                if (!NET_WOULD_BLOCK(errorCode))
                {
                    disconnect_additional_host_peer(state, i, NULL);
                }
            }
        }
    }
}

static void flush_send_queue(struct NetplayState *state)
{
    if (state != NULL && state->relayTransport)
    {
        while (state->connectionState == NETPLAY_CONNECTION_CONNECTED && state->sendCount > 0)
        {
            struct NetplayQueuedPacket *packet = &state->sendQueue[state->sendHead];
            if (send_relay_message(state, packet->bytes, packet->length, true) == NET_SOCKET_ERROR)
            {
                disconnect_peer(state, false, "send failed");
                return;
            }

            state->sendHead = (state->sendHead + 1) % NETPLAY_SEND_QUEUE_CAPACITY;
            state->sendCount--;
            state->lastSendMs = netplay_now_ms();
        }

        return;
    }

    while (state != NULL && state->connectionState == NETPLAY_CONNECTION_CONNECTED &&
           (state->relayHandshakeOffset < state->relayHandshakeLength || state->sendCount > 0))
    {
        struct NetplayQueuedPacket *packet = NULL;
        NetSocket targetSocket = NET_INVALID_SOCKET;
        const char *bytes = NULL;
        int bytesRemaining = 0;
        int sent = 0;

        if (state->relayHandshakeOffset < state->relayHandshakeLength)
        {
            targetSocket = state->peerSocket;
            bytes = state->relayHandshake + state->relayHandshakeOffset;
            bytesRemaining = (int)(state->relayHandshakeLength - state->relayHandshakeOffset);
        }
        else
        {
            packet = &state->sendQueue[state->sendHead];
            if (state->mode == NETPLAY_MODE_HOST)
            {
                targetSocket = get_host_peer_socket_by_id(state, packet->peerId);
                if (targetSocket == NET_INVALID_SOCKET)
                {
                    packet->offset = packet->length;
                    state->sendHead = (state->sendHead + 1) % NETPLAY_SEND_QUEUE_CAPACITY;
                    state->sendCount--;
                    continue;
                }
            }
            else
            {
                targetSocket = state->peerSocket;
                if (targetSocket == NET_INVALID_SOCKET)
                {
                    disconnect_peer(state, false, "peer unavailable");
                    return;
                }
            }

            bytes = (const char *)(packet->bytes + packet->offset);
            bytesRemaining = (int)(packet->length - packet->offset);
        }

        sent = transport_send(targetSocket, bytes, bytesRemaining);
        if (sent == NET_SOCKET_ERROR)
        {
            const int errorCode = net_last_error_code();
            if (NET_WOULD_BLOCK(errorCode))
            {
                return;
            }

            if (state->mode == NETPLAY_MODE_HOST)
            {
                if (packet != NULL)
                {
                    disconnect_host_peer(state, packet->peerId, "send failed");
                    packet->offset = packet->length;
                    state->sendHead = (state->sendHead + 1) % NETPLAY_SEND_QUEUE_CAPACITY;
                    state->sendCount--;
                }
                continue;
            }

            disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "send failed");
            return;
        }

        if (sent <= 0)
        {
            if (state->mode == NETPLAY_MODE_HOST)
            {
                if (packet != NULL)
                {
                    disconnect_host_peer(state, packet->peerId, "peer closed");
                    packet->offset = packet->length;
                    state->sendHead = (state->sendHead + 1) % NETPLAY_SEND_QUEUE_CAPACITY;
                    state->sendCount--;
                }
                continue;
            }

            disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "peer closed");
            return;
        }

        if (state->relayHandshakeOffset < state->relayHandshakeLength)
        {
            state->relayHandshakeOffset += (size_t)sent;
            if (state->relayHandshakeOffset >= state->relayHandshakeLength)
            {
                clear_relay_handshake(state);
            }
        }
        else if (packet != NULL)
        {
            packet->offset += (size_t)sent;
        }
        state->lastSendMs = netplay_now_ms();
        if (packet != NULL && packet->offset >= packet->length)
        {
            state->sendHead = (state->sendHead + 1) % NETPLAY_SEND_QUEUE_CAPACITY;
            state->sendCount--;
        }
    }
}

static void accept_pending_client(struct NetplayState *state)
{
    struct sockaddr_in address;
#ifdef _WIN32
    int addressLength = (int)sizeof(address);
#else
    NetSockLen addressLength = (NetSockLen)sizeof(address);
#endif
    NetSocket accepted = NET_INVALID_SOCKET;

    if (state == NULL || state->mode != NETPLAY_MODE_HOST || state->relayTransport || state->listenSocket == NET_INVALID_SOCKET)
    {
        return;
    }

    accepted = transport_accept(state->listenSocket, &address, &addressLength);
    if (accepted == NET_INVALID_SOCKET)
    {
        const int errorCode = net_last_error_code();
        if (!NET_WOULD_BLOCK(errorCode))
        {
            set_last_error(state, "accept failed");
            state->connectionState = NETPLAY_CONNECTION_ERROR;
        }
        return;
    }

    if (!set_socket_nonblocking(accepted))
    {
        net_close_socket(accepted);
        set_last_error(state, "nonblocking accept failed");
        return;
    }

    set_socket_nodelay(accepted);
    if (state->peerSocket == NET_INVALID_SOCKET)
    {
        state->peerSocket = accepted;
        state->connectionState = NETPLAY_CONNECTION_CONNECTED;
        state->connectStartedMs = 0u;
        snprintf(state->peerAddress, sizeof(state->peerAddress), "%s", inet_ntoa(address.sin_addr));
        clear_last_error(state);

        {
            struct NetplayEvent event;
            memset(&event, 0, sizeof(event));
            event.type = NETPLAY_EVENT_CLIENT_CONNECTED;
            event.peerId = 0;
            push_event(state, &event);
        }
    }
    else if (!try_track_additional_host_peer(state, accepted, inet_ntoa(address.sin_addr)))
    {
        net_close_socket(accepted);
    }
}

static void advance_pending_connect(struct NetplayState *state)
{
    int socketError = 0;
#ifdef _WIN32
    int optionLength = (int)sizeof(socketError);
#else
    NetSockLen optionLength = (NetSockLen)sizeof(socketError);
#endif

    if (state == NULL || state->mode != NETPLAY_MODE_CLIENT || state->connectionState != NETPLAY_CONNECTION_CONNECTING || state->peerSocket == NET_INVALID_SOCKET)
    {
        return;
    }

    if (state->connectStartedMs > 0u &&
        netplay_now_ms() - state->connectStartedMs >= NETPLAY_CONNECT_TIMEOUT_MS)
    {
        if (is_loopback_host(state->peerAddress))
        {
            set_last_error(state, "connect timed out (use host LAN IP)");
        }
        else
        {
            set_last_error(state, "connect timed out (check host IP, port, firewall)");
        }
        close_socket_if_open(&state->peerSocket);
        state->connectStartedMs = 0u;
        state->connectionState = NETPLAY_CONNECTION_ERROR;
        return;
    }

    if (!socket_is_writable(state->peerSocket))
    {
        return;
    }

    if (getsockopt(state->peerSocket, SOL_SOCKET, SO_ERROR, (char *)&socketError, &optionLength) != 0 || socketError != 0)
    {
        char errorText[NETPLAY_MAX_STATUS_TEXT];
        if (is_loopback_host(state->peerAddress))
        {
            snprintf(errorText, sizeof(errorText), "%s", "connect failed (use host LAN IP)");
        }
        else
        {
            snprintf(errorText, sizeof(errorText), "connect failed (code %d)", socketError);
        }
        set_last_error(state, errorText);
        close_socket_if_open(&state->peerSocket);
        state->connectStartedMs = 0u;
        state->connectionState = NETPLAY_CONNECTION_ERROR;
        return;
    }

    state->connectionState = NETPLAY_CONNECTION_CONNECTED;
    state->connectStartedMs = 0u;
    if (state->relayTransport)
    {
        prepare_relay_handshake(state, false, NULL);
    }
    if (state->relayTransport)
    {
        prepare_relay_handshake(state, state->mode == NETPLAY_MODE_HOST, NULL);
    }
    clear_last_error(state);
    {
        struct NetplayEvent event;
        memset(&event, 0, sizeof(event));
        event.type = NETPLAY_EVENT_CONNECTED;
        event.peerId = 0;
        push_event(state, &event);
    }
}

static void read_from_peer(struct NetplayState *state)
{
    if (state != NULL && state->relayTransport)
    {
        const int remainingCapacity = (int)(sizeof(state->recvBuffer) - state->recvLength);
        const int received = receive_relay_message(state, state->recvBuffer + state->recvLength, (size_t)remainingCapacity);

        if (received == 0)
        {
            disconnect_peer(state, false, "peer disconnected");
            return;
        }

        if (received == NET_SOCKET_ERROR)
        {
            disconnect_peer(state, false, "receive failed");
            return;
        }

        if (received == -2)
        {
            return;
        }

        state->recvLength += (size_t)received;
        state->lastReceiveMs = netplay_now_ms();
        if (state->recvLength >= sizeof(state->recvBuffer))
        {
            disconnect_peer(state, false, "receive buffer overflow");
        }

        return;
    }

    while (state != NULL &&
           state->peerSocket != NET_INVALID_SOCKET &&
           state->connectionState == NETPLAY_CONNECTION_CONNECTED)
    {
        const int remainingCapacity = (int)(sizeof(state->recvBuffer) - state->recvLength);
        const int received = transport_recv(state->peerSocket, (char *)(state->recvBuffer + state->recvLength), remainingCapacity, 0);

        if (received == 0)
        {
            if (state->mode == NETPLAY_MODE_HOST)
            {
                disconnect_host_peer(state, 0, "peer disconnected");
            }
            else
            {
                disconnect_peer(state, false, "peer disconnected");
            }
            return;
        }

        if (received == NET_SOCKET_ERROR)
        {
            const int errorCode = net_last_error_code();
            if (NET_WOULD_BLOCK(errorCode))
            {
                return;
            }

            if (state->mode == NETPLAY_MODE_HOST)
            {
                disconnect_host_peer(state, 0, "receive failed");
            }
            else
            {
                disconnect_peer(state, false, "receive failed");
            }
            return;
        }

        if (received < 0)
        {
            return;
        }

        state->recvLength += (size_t)received;
        state->lastReceiveMs = netplay_now_ms();
        if (state->recvLength >= sizeof(state->recvBuffer))
        {
            disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "receive buffer overflow");
            return;
        }
    }
}

static void encode_action(const struct GameAction *action, struct NetplayActionWire *wireAction)
{
    if (action == NULL || wireAction == NULL)
    {
        return;
    }

    wireAction->type = encode_i32(action->type);
    wireAction->tileId = encode_i32(action->tileId);
    wireAction->cornerIndex = encode_i32(action->cornerIndex);
    wireAction->sideIndex = encode_i32(action->sideIndex);
    wireAction->diceRoll = encode_i32(action->diceRoll);
    wireAction->player = encode_i32(action->player);
    wireAction->resourceA = encode_i32(action->resourceA);
    wireAction->resourceB = encode_i32(action->resourceB);
    wireAction->amountA = encode_i32(action->amountA);
    wireAction->amountB = encode_i32(action->amountB);
    for (int resource = 0; resource < 5; resource++)
    {
        wireAction->resources[resource] = encode_i32(action->resources[resource]);
    }
}

static void decode_action(struct GameAction *action, const struct NetplayActionWire *wireAction)
{
    if (action == NULL || wireAction == NULL)
    {
        return;
    }

    memset(action, 0, sizeof(*action));
    action->type = (enum GameActionType)decode_i32(wireAction->type);
    action->tileId = decode_i32(wireAction->tileId);
    action->cornerIndex = decode_i32(wireAction->cornerIndex);
    action->sideIndex = decode_i32(wireAction->sideIndex);
    action->diceRoll = decode_i32(wireAction->diceRoll);
    action->player = (enum PlayerType)decode_i32(wireAction->player);
    action->resourceA = (enum ResourceType)decode_i32(wireAction->resourceA);
    action->resourceB = (enum ResourceType)decode_i32(wireAction->resourceB);
    action->amountA = decode_i32(wireAction->amountA);
    action->amountB = decode_i32(wireAction->amountB);
    for (int resource = 0; resource < 5; resource++)
    {
        action->resources[resource] = decode_i32(wireAction->resources[resource]);
    }
}

static void encode_action_result(const struct GameActionResult *result, struct NetplayActionResultWire *wireResult)
{
    if (result == NULL || wireResult == NULL)
    {
        return;
    }

    wireResult->applied = encode_bool(result->applied);
    wireResult->diceRoll = encode_i32(result->diceRoll);
    wireResult->drawnCard = encode_i32(result->drawnCard);
    wireResult->stolenResource = encode_i32(result->stolenResource);
}

static void decode_action_result(struct GameActionResult *result, const struct NetplayActionResultWire *wireResult)
{
    if (result == NULL || wireResult == NULL)
    {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->applied = decode_bool(wireResult->applied);
    result->diceRoll = decode_i32(wireResult->diceRoll);
    result->drawnCard = (enum DevelopmentCardType)decode_i32(wireResult->drawnCard);
    result->stolenResource = (enum ResourceType)decode_i32(wireResult->stolenResource);
}

static bool action_is_sane(const struct GameAction *action)
{
    if (action == NULL)
    {
        return false;
    }

    if (action->type < GAME_ACTION_NONE || action->type > GAME_ACTION_TRADE_WITH_PLAYER)
    {
        return false;
    }

    if (action->tileId < -1 || action->tileId >= MAX_TILES ||
        action->cornerIndex < -1 || action->cornerIndex >= 6 ||
        action->sideIndex < -1 || action->sideIndex >= 6 ||
        action->diceRoll < 0 || action->diceRoll > 12 ||
        action->player < PLAYER_NONE || action->player > PLAYER_BLACK ||
        action->resourceA < RESOURCE_WOOD || action->resourceA > RESOURCE_STONE ||
        action->resourceB < RESOURCE_WOOD || action->resourceB > RESOURCE_STONE ||
        action->amountA < 0 || action->amountA > 64 ||
        action->amountB < 0 || action->amountB > 64)
    {
        return false;
    }

    for (int resource = 0; resource < 5; resource++)
    {
        if (action->resources[resource] < 0 || action->resources[resource] > 64)
        {
            return false;
        }
    }

    return true;
}

static bool action_result_is_sane(const struct GameActionResult *result)
{
    if (result == NULL)
    {
        return false;
    }

    return result->diceRoll >= 0 &&
           result->diceRoll <= 12 &&
           result->drawnCard >= DEVELOPMENT_CARD_KNIGHT &&
           result->drawnCard <= DEVELOPMENT_CARD_COUNT &&
           result->stolenResource >= RESOURCE_WOOD &&
           result->stolenResource <= RESOURCE_STONE;
}

static bool decode_packet(struct NetplayState *state,
                          int peerId,
                          enum NetplayPacketType type,
                          const unsigned char *payload,
                          size_t payloadSize)
{
    struct NetplayEvent event;

    if (state == NULL || payload == NULL)
    {
    return false;
    }

    memset(&event, 0, sizeof(event));
    event.peerId = peerId;
    switch (type)
    {
    case NETPLAY_PACKET_HELLO:
        if (payloadSize != sizeof(struct NetplayHelloWire))
        {
            return false;
        }
        {
            struct NetplayHelloWire wireHello;
            memcpy(&wireHello, payload, sizeof(wireHello));
            event.type = NETPLAY_EVENT_HELLO;
            event.hello.assignedPlayer = (enum PlayerType)decode_i32(wireHello.assignedPlayer);
            event.hello.hostPlayer = (enum PlayerType)decode_i32(wireHello.hostPlayer);
            for (int player = 0; player < MAX_PLAYERS; player++)
            {
                event.hello.seatAuthority[player] = decode_i32(wireHello.seatAuthority[player]);
            }
        }
        break;

    case NETPLAY_PACKET_LOBBY_STATE:
        if (payloadSize != sizeof(struct NetplayLobbyStateWire))
        {
            return false;
        }
        event.type = NETPLAY_EVENT_LOBBY_STATE;
        decode_lobby_state(&event.lobbyState, (const struct NetplayLobbyStateWire *)payload);
        break;

    case NETPLAY_PACKET_MATCH_INIT:
        if (payloadSize != sizeof(struct NetplayMatchInitWire))
        {
            return false;
        }
        event.type = NETPLAY_EVENT_MATCH_INIT;
        decode_match_init(&event.matchInit, (const struct NetplayMatchInitWire *)payload);
        break;

    case NETPLAY_PACKET_SNAPSHOT:
        event.type = NETPLAY_EVENT_SNAPSHOT;
        event.payloadSize = payloadSize;
        memcpy(event.payload, payload, payloadSize);
        break;

    case NETPLAY_PACKET_ACTION_REQUEST:
        if (payloadSize != sizeof(struct NetplayActionWire))
        {
            return false;
        }
        event.type = NETPLAY_EVENT_ACTION_REQUEST;
        decode_action(&event.action, (const struct NetplayActionWire *)payload);
        if (!action_is_sane(&event.action))
        {
            return false;
        }
        break;

    case NETPLAY_PACKET_ACTION_RESULT:
        if (payloadSize != sizeof(struct NetplayActionOutcomeWire))
        {
            return false;
        }
        {
            struct NetplayActionOutcomeWire wireOutcome;
            memcpy(&wireOutcome, payload, sizeof(wireOutcome));
            event.type = NETPLAY_EVENT_ACTION_RESULT;
            decode_action(&event.action, &wireOutcome.action);
            decode_action_result(&event.result, &wireOutcome.result);
            if (!action_is_sane(&event.action) || !action_result_is_sane(&event.result))
            {
                return false;
            }
            event.stateHash = decode_u32(wireOutcome.stateHash);
        }
        break;

    case NETPLAY_PACKET_ACTION_REJECT:
        if (payloadSize != sizeof(struct NetplayRejectWire))
        {
            return false;
        }
        {
            struct NetplayRejectWire wireReject;
            memcpy(&wireReject, payload, sizeof(wireReject));
            event.type = NETPLAY_EVENT_ACTION_REJECT;
            event.stateHash = decode_u32(wireReject.stateHash);
            snprintf(event.message, sizeof(event.message), "%s", wireReject.message);
        }
        break;

    case NETPLAY_PACKET_TRADE_OFFER:
        if (payloadSize != sizeof(struct NetplayActionWire))
        {
            return false;
        }
        event.type = NETPLAY_EVENT_TRADE_OFFER;
        decode_action(&event.action, (const struct NetplayActionWire *)payload);
        if (!action_is_sane(&event.action))
        {
            return false;
        }
        break;

    case NETPLAY_PACKET_TRADE_RESPONSE:
        if (payloadSize != sizeof(struct NetplayTradeResponseWire))
        {
            return false;
        }
        {
            struct NetplayTradeResponseWire wireResponse;
            memcpy(&wireResponse, payload, sizeof(wireResponse));
            event.type = NETPLAY_EVENT_TRADE_RESPONSE;
            decode_action(&event.action, &wireResponse.action);
            if (!action_is_sane(&event.action))
            {
                return false;
            }
            event.accepted = decode_bool(wireResponse.accepted);
            event.stateHash = decode_u32(wireResponse.stateHash);
        }
        break;

    case NETPLAY_PACKET_CAPABILITIES:
        if (payloadSize != sizeof(struct NetplayCapabilitiesWire))
        {
            return false;
        }
        {
            struct NetplayCapabilitiesWire wireCaps;
            memcpy(&wireCaps, payload, sizeof(wireCaps));
            event.type = NETPLAY_EVENT_CAPABILITIES;
            event.capabilityFlags = decode_u32(wireCaps.capabilityFlags);
            event.protocolMinVersion = decode_u32(wireCaps.protocolMinVersion);
            event.protocolMaxVersion = decode_u32(wireCaps.protocolMaxVersion);
        }
        break;

    case NETPLAY_PACKET_HEARTBEAT:
        if (payloadSize != 0u)
        {
            return false;
        }
        return true;

    case NETPLAY_PACKET_RESYNC_REQUEST:
        if (payloadSize != sizeof(struct NetplayResyncRequestWire))
        {
            return false;
        }
        {
            struct NetplayResyncRequestWire wireResync;
            memcpy(&wireResync, payload, sizeof(wireResync));
            event.type = NETPLAY_EVENT_RESYNC_REQUEST;
            event.stateHash = decode_u32(wireResync.stateHash);
            snprintf(event.message, sizeof(event.message), "%s", wireResync.reason);
        }
        break;

    default:
        return false;
    }

    return push_event(state, &event);
}

static void parse_incoming_packets(struct NetplayState *state)
{
    char failureReason[NETPLAY_MAX_STATUS_TEXT];

    if (state == NULL)
    {
        return;
    }

    if (!parse_incoming_packets_for_peer(state,
                                         0,
                                         state->recvBuffer,
                                         &state->recvLength,
                                         failureReason,
                                         sizeof(failureReason)))
    {
        if (state->mode == NETPLAY_MODE_HOST)
        {
            disconnect_host_peer(state, 0, failureReason);
        }
        else
        {
            disconnect_peer(state, false, failureReason);
        }
    }
}

static bool parse_incoming_packets_for_peer(struct NetplayState *state,
                                            int peerId,
                                            unsigned char *recvBuffer,
                                            size_t *recvLength,
                                            char *failureReason,
                                            size_t failureReasonSize)
{
    if (state == NULL || recvBuffer == NULL || recvLength == NULL)
    {
        return false;
    }

    if (failureReason != NULL && failureReasonSize > 0u)
    {
        failureReason[0] = '\0';
    }

    if (state->eventCount >= NETPLAY_EVENT_QUEUE_CAPACITY && *recvLength >= sizeof(struct NetplayPacketHeader))
    {
        if (failureReason != NULL && failureReasonSize > 0u)
        {
            snprintf(failureReason, failureReasonSize, "%s", "event queue overflow");
        }
        return false;
    }

    while (*recvLength >= sizeof(struct NetplayPacketHeader) && state->eventCount < NETPLAY_EVENT_QUEUE_CAPACITY)
    {
        struct NetplayPacketHeader header;
        size_t payloadSize = 0u;
        enum NetplayPacketType type = NETPLAY_PACKET_HELLO;

        memcpy(&header, recvBuffer, sizeof(header));
        payloadSize = (size_t)decode_u32(header.payloadSize);
        type = (enum NetplayPacketType)decode_u32(header.type);
        if (memcmp(header.magic, NETPLAY_PACKET_MAGIC, sizeof(header.magic)) != 0 ||
            payloadSize > NETPLAY_MAX_PAYLOAD_SIZE)
        {
            if (failureReason != NULL && failureReasonSize > 0u)
            {
                snprintf(failureReason, failureReasonSize, "%s", "invalid packet");
            }
            return false;
        }

        if (decode_u32(header.version) != NETPLAY_PROTOCOL_VERSION)
        {
            if (failureReason != NULL && failureReasonSize > 0u)
            {
                snprintf(failureReason, failureReasonSize, "%s", "protocol mismatch");
            }
            return false;
        }

        {
            const size_t fullPacketLength = sizeof(header) + payloadSize;
            const unsigned char *payload = NULL;
            if (*recvLength < fullPacketLength)
            {
                return true;
            }

            payload = recvBuffer + sizeof(header);
            if (!decode_packet(state, peerId, type, payload, payloadSize))
            {
                if (failureReason != NULL && failureReasonSize > 0u)
                {
                    snprintf(failureReason, failureReasonSize, "%s", "packet decode failed");
                }
                return false;
            }

            memmove(recvBuffer, recvBuffer + fullPacketLength, *recvLength - fullPacketLength);
            *recvLength -= fullPacketLength;
        }
    }

    return true;
}

static void read_from_additional_host_peers(struct NetplayState *state)
{
    if (state == NULL || state->mode != NETPLAY_MODE_HOST || state->connectionState != NETPLAY_CONNECTION_CONNECTED)
    {
        return;
    }

    for (int i = 0; i < NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1; i++)
    {
        NetSocket peerSocket = state->trackedHostPeers[i];
        char failureReason[NETPLAY_MAX_STATUS_TEXT];

        if (peerSocket == NET_INVALID_SOCKET)
        {
            continue;
        }

        while (true)
        {
            const int remainingCapacity = (int)(NETPLAY_RECV_BUFFER_SIZE - state->trackedHostPeerRecvLength[i]);
            const int received = transport_recv(peerSocket,
                                      (char *)(state->trackedHostPeerRecvBuffer[i] + state->trackedHostPeerRecvLength[i]),
                                      remainingCapacity,
                                      0);

            if (received == 0)
            {
                disconnect_additional_host_peer(state, i, "peer disconnected");
                break;
            }

            if (received == NET_SOCKET_ERROR)
            {
                const int errorCode = net_last_error_code();
                if (NET_WOULD_BLOCK(errorCode))
                {
                    break;
                }

                disconnect_additional_host_peer(state, i, "receive failed");
                break;
            }

            if (received < 0)
            {
                break;
            }

            state->trackedHostPeerRecvLength[i] += (size_t)received;
            state->trackedHostPeerLastReceiveMs[i] = netplay_now_ms();
            if (state->trackedHostPeerRecvLength[i] >= NETPLAY_RECV_BUFFER_SIZE)
            {
                disconnect_additional_host_peer(state, i, "receive buffer overflow");
                break;
            }
        }

        if (state->trackedHostPeers[i] == NET_INVALID_SOCKET)
        {
            continue;
        }

        if (!parse_incoming_packets_for_peer(state,
                                             i + 1,
                                             state->trackedHostPeerRecvBuffer[i],
                                             &state->trackedHostPeerRecvLength[i],
                                             failureReason,
                                             sizeof(failureReason)))
        {
            disconnect_additional_host_peer(state, i, failureReason);
        }
    }
}

static void process_connection_health(struct NetplayState *state)
{
    const uint64_t nowMs = netplay_now_ms();

    if (state == NULL || state->connectionState != NETPLAY_CONNECTION_CONNECTED)
    {
        return;
    }

    if (state->mode != NETPLAY_MODE_HOST &&
        state->lastReceiveMs > 0u &&
        nowMs - state->lastReceiveMs > NETPLAY_STALE_TIMEOUT_MS)
    {
        disconnect_peer(state, false, "connection timed out");
        return;
    }

    if (state->mode == NETPLAY_MODE_HOST &&
        state->peerSocket != NET_INVALID_SOCKET &&
        state->lastReceiveMs > 0u &&
        nowMs - state->lastReceiveMs > NETPLAY_STALE_TIMEOUT_MS)
    {
        disconnect_host_peer(state, 0, "connection timed out");
    }

    if (state->mode == NETPLAY_MODE_HOST)
    {
        for (int i = 0; i < NETPLAY_MAX_HOST_REMOTE_PLAYERS - 1; i++)
        {
            if (state->trackedHostPeers[i] != NET_INVALID_SOCKET &&
                state->trackedHostPeerLastReceiveMs[i] > 0u &&
                nowMs - state->trackedHostPeerLastReceiveMs[i] > NETPLAY_STALE_TIMEOUT_MS)
            {
                disconnect_additional_host_peer(state, i, "connection timed out");
            }
        }
    }

    if ((state->lastHeartbeatSentMs == 0u || nowMs - state->lastHeartbeatSentMs >= NETPLAY_HEARTBEAT_INTERVAL_MS) &&
        state->sendCount < NETPLAY_SEND_QUEUE_CAPACITY)
    {
        const bool queued = state->mode == NETPLAY_MODE_HOST
                                ? queue_packet_to_all_host_peers(state, NETPLAY_PACKET_HEARTBEAT, NULL, 0u)
                                : queue_packet(state, NETPLAY_PACKET_HEARTBEAT, NULL, 0u);
        if (queued)
        {
            state->lastHeartbeatSentMs = nowMs;
        }
    }
}

struct NetplayState *netplayCreate(void)
{
    struct NetplayState *state = NULL;

    if (!net_socket_layer_init())
    {
        return NULL;
    }

    state = (struct NetplayState *)calloc(1, sizeof(*state));
    if (state == NULL)
    {
        return NULL;
    }

    state->listenSocket = NET_INVALID_SOCKET;
    state->peerSocket = NET_INVALID_SOCKET;
#ifdef _WIN32
    state->relaySession = NULL;
    state->relayConnection = NULL;
    state->relayRequest = NULL;
    state->relayWebSocket = NULL;
#endif
    init_tracked_host_peers(state);
    state->connectionState = NETPLAY_CONNECTION_IDLE;
    state->relayTransport = false;
    state->relayRoomCode[0] = '\0';
    state->connectStartedMs = 0u;
    clear_relay_handshake(state);
    return state;
}

void netplayDestroy(struct NetplayState *state)
{
    if (state == NULL)
    {
        return;
    }

    close_socket_if_open(&state->peerSocket);
    close_tracked_host_peers(state);
    close_relay_transport(state);
    close_socket_if_open(&state->listenSocket);
    free(state);
}

bool netplayStartHost(struct NetplayState *state, unsigned short port)
{
    struct sockaddr_in address;
    int reuseAddress = 1;

    if (state == NULL)
    {
        return false;
    }

    close_socket_if_open(&state->peerSocket);
    close_tracked_host_peers(state);
    close_socket_if_open(&state->listenSocket);
    close_relay_transport(state);
    reset_connection_buffers(state);
    clear_relay_handshake(state);
    state->relayRoomCode[0] = '\0';
    state->eventHead = 0;
    state->eventCount = 0;
    state->connectStartedMs = 0u;
    state->mode = NETPLAY_MODE_HOST;
    state->relayTransport = false;
    state->port = port;
    detect_local_ipv4_address(state->localAddress, sizeof(state->localAddress));
    state->listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (state->listenSocket == NET_INVALID_SOCKET)
    {
        set_last_error(state, "socket failed");
        state->connectionState = NETPLAY_CONNECTION_ERROR;
        return false;
    }

    setsockopt(state->listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuseAddress, (NetSockLen)sizeof(reuseAddress));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(state->listenSocket, (const struct sockaddr *)&address, sizeof(address)) == NET_SOCKET_ERROR)
    {
        close_socket_if_open(&state->listenSocket);
        set_last_error(state, "bind failed");
        state->connectionState = NETPLAY_CONNECTION_ERROR;
        return false;
    }

    if (listen(state->listenSocket, NETPLAY_MAX_HOST_REMOTE_PLAYERS) == NET_SOCKET_ERROR || !set_socket_nonblocking(state->listenSocket))
    {
        close_socket_if_open(&state->listenSocket);
        set_last_error(state, "listen failed");
        state->connectionState = NETPLAY_CONNECTION_ERROR;
        return false;
    }

    clear_last_error(state);
    state->connectionState = NETPLAY_CONNECTION_LISTENING;
    return true;
}

static bool start_outbound_connection(struct NetplayState *state,
                                      const char *hostAddress,
                                      unsigned short port,
                                      enum NetplayMode mode,
                                      bool relayTransport,
                                      bool isRelayHost,
                                      const char *roomCode)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *current = NULL;
    char resolvedHost[NETPLAY_MAX_PEER_ADDRESS + 1];
    char portText[16];
    unsigned short resolvedPort = port;
    bool secureRelayTransport = false;
    int lookupResult = 0;

    if (state == NULL || hostAddress == NULL || hostAddress[0] == '\0')
    {
        return false;
    }

    close_socket_if_open(&state->peerSocket);
    close_tracked_host_peers(state);
    close_socket_if_open(&state->listenSocket);
    close_relay_transport(state);
    reset_connection_buffers(state);
    clear_relay_handshake(state);
    state->relayRoomCode[0] = '\0';
    state->eventHead = 0;
    state->eventCount = 0;
    state->connectStartedMs = 0u;
    state->mode = mode;
    state->relayTransport = relayTransport;
    if (relayTransport && roomCode != NULL && roomCode[0] != '\0')
    {
        snprintf(state->relayRoomCode, sizeof(state->relayRoomCode), "%s", roomCode);
    }
    detect_local_ipv4_address(state->localAddress, sizeof(state->localAddress));

    if (relayTransport)
    {
        if (!parse_relay_endpoint(hostAddress,
                                  port,
                                  resolvedHost,
                                  sizeof(resolvedHost),
                                  &resolvedPort,
                                  &secureRelayTransport))
        {
            set_last_error(state, "invalid relay address");
            state->connectionState = NETPLAY_CONNECTION_ERROR;
            return false;
        }
        state->port = resolvedPort;
        snprintf(state->peerAddress, sizeof(state->peerAddress), "%s", resolvedHost);
    }
    else
    {
        state->port = port;
        snprintf(state->peerAddress, sizeof(state->peerAddress), "%s", hostAddress);
    }

#ifndef _WIN32
    if (relayTransport && secureRelayTransport)
    {
        set_last_error(state, "secure relay transport is supported only on Windows builds");
        state->connectionState = NETPLAY_CONNECTION_ERROR;
        return false;
    }
#endif

#ifdef _WIN32
    if (relayTransport)
    {
        if (!open_relay_transport(state,
                                  state->peerAddress,
                                  state->port,
                                  secureRelayTransport,
                                  isRelayHost,
                                  roomCode))
        {
            state->connectionState = NETPLAY_CONNECTION_ERROR;
            state->connectStartedMs = 0u;
            return false;
        }

        state->connectionState = NETPLAY_CONNECTION_CONNECTED;
        state->connectStartedMs = 0u;
        clear_last_error(state);
        {
            struct NetplayEvent event;
            memset(&event, 0, sizeof(event));
            event.type = NETPLAY_EVENT_CONNECTED;
            event.peerId = 0;
            push_event(state, &event);
        }
        return true;
    }
#endif

    snprintf(portText, sizeof(portText), "%hu", state->port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    lookupResult = getaddrinfo(state->peerAddress, portText, &hints, &results);
    if (lookupResult != 0 || results == NULL)
    {
        if (is_loopback_host(state->peerAddress))
        {
            set_last_error(state, "host lookup failed (use host LAN IP)");
        }
        else
        {
            set_last_error(state, "host lookup failed");
        }
        state->connectionState = NETPLAY_CONNECTION_ERROR;
        return false;
    }

    for (current = results; current != NULL; current = current->ai_next)
    {
        int connectResult = 0;
        state->peerSocket = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (state->peerSocket == NET_INVALID_SOCKET)
        {
            continue;
        }

        if (!set_socket_nonblocking(state->peerSocket))
        {
            close_socket_if_open(&state->peerSocket);
            continue;
        }

        set_socket_nodelay(state->peerSocket);
        connectResult = connect(state->peerSocket, current->ai_addr, (NetSockLen)current->ai_addrlen);
        if (connectResult == 0)
        {
            state->connectionState = NETPLAY_CONNECTION_CONNECTED;
            state->connectStartedMs = 0u;
            if (relayTransport)
            {
                prepare_relay_handshake(state, isRelayHost, roomCode);
            }
            clear_last_error(state);
            {
                struct NetplayEvent event;
                memset(&event, 0, sizeof(event));
                event.type = NETPLAY_EVENT_CONNECTED;
                event.peerId = 0;
                push_event(state, &event);
            }
            freeaddrinfo(results);
            return true;
        }

        {
            const int errorCode = net_last_error_code();
            if (NET_CONNECT_IN_PROGRESS(errorCode))
            {
                state->connectionState = NETPLAY_CONNECTION_CONNECTING;
                state->connectStartedMs = netplay_now_ms();
                clear_last_error(state);
                freeaddrinfo(results);
                return true;
            }
        }

        close_socket_if_open(&state->peerSocket);
    }

    freeaddrinfo(results);
    if (is_loopback_host(state->peerAddress))
    {
        set_last_error(state, "connect failed (use host LAN IP)");
    }
    else
    {
        set_last_error(state, "connect failed");
    }
    state->connectionState = NETPLAY_CONNECTION_ERROR;
    state->connectStartedMs = 0u;
    return false;
}

bool netplayStartClient(struct NetplayState *state, const char *hostAddress, unsigned short port)
{
    return start_outbound_connection(state, hostAddress, port, NETPLAY_MODE_CLIENT, false, false, NULL);
}

bool netplayStartRelayHost(struct NetplayState *state,
                           const char *relayAddress,
                           unsigned short port,
                           const char *roomCode)
{
    return start_outbound_connection(state,
                                     relayAddress,
                                     port,
                                     NETPLAY_MODE_HOST,
                                     true,
                                     true,
                                     roomCode);
}

bool netplayStartRelayClient(struct NetplayState *state,
                             const char *relayAddress,
                             unsigned short port,
                             const char *roomCode)
{
    return start_outbound_connection(state,
                                     relayAddress,
                                     port,
                                     NETPLAY_MODE_CLIENT,
                                     true,
                                     false,
                                     roomCode);
}

void netplayUpdate(struct NetplayState *state)
{
    if (state == NULL)
    {
        return;
    }

    accept_pending_client(state);
    advance_pending_connect(state);
    flush_send_queue(state);
    read_from_peer(state);
    read_from_additional_host_peers(state);
    parse_incoming_packets(state);
    poll_tracked_host_peers(state);
    process_connection_health(state);
}

bool netplayPollEvent(struct NetplayState *state, struct NetplayEvent *event)
{
    if (state == NULL || event == NULL || state->eventCount <= 0)
    {
        return false;
    }

    *event = state->eventQueue[state->eventHead];
    state->eventHead = (state->eventHead + 1) % NETPLAY_EVENT_QUEUE_CAPACITY;
    state->eventCount--;
    return true;
}

bool netplayQueueHello(struct NetplayState *state,
                       enum PlayerType assignedPlayer,
                       enum PlayerType hostPlayer,
                       const int seatAuthority[MAX_PLAYERS])
{
    return netplayQueueHelloForHostPeer(state, 0, assignedPlayer, hostPlayer, seatAuthority);
}

bool netplayQueueHelloForHostPeer(struct NetplayState *state,
                                  int peerId,
                                  enum PlayerType assignedPlayer,
                                  enum PlayerType hostPlayer,
                                  const int seatAuthority[MAX_PLAYERS])
{
    struct NetplayHelloWire wireHello;

    if (state == NULL || seatAuthority == NULL)
    {
        return false;
    }

    wireHello.assignedPlayer = encode_i32(assignedPlayer);
    wireHello.hostPlayer = encode_i32(hostPlayer);
    for (int player = 0; player < MAX_PLAYERS; player++)
    {
        wireHello.seatAuthority[player] = encode_i32(seatAuthority[player]);
    }

    if (state->mode == NETPLAY_MODE_HOST)
    {
        return queue_packet_to_peer(state, peerId, NETPLAY_PACKET_HELLO, &wireHello, sizeof(wireHello));
    }

    return queue_packet(state, NETPLAY_PACKET_HELLO, &wireHello, sizeof(wireHello));
}

bool netplayQueueLobbyState(struct NetplayState *state, const struct NetplayLobbyStateInfo *info)
{
    if (state != NULL && state->mode == NETPLAY_MODE_HOST)
    {
        return netplayQueueLobbyStateForHostPeer(state, -1, info);
    }

    return netplayQueueLobbyStateForHostPeer(state, 0, info);
}

bool netplayQueueLobbyStateForHostPeer(struct NetplayState *state,
                                       int peerId,
                                       const struct NetplayLobbyStateInfo *info)
{
    struct NetplayLobbyStateWire wireInfo;

    if (state == NULL || info == NULL)
    {
        return false;
    }

    memset(&wireInfo, 0, sizeof(wireInfo));
    encode_lobby_state(info, &wireInfo);

    if (state->mode == NETPLAY_MODE_HOST)
    {
        if (peerId < 0)
        {
            return queue_packet_to_all_host_peers(state, NETPLAY_PACKET_LOBBY_STATE, &wireInfo, sizeof(wireInfo));
        }
        return queue_packet_to_peer(state, peerId, NETPLAY_PACKET_LOBBY_STATE, &wireInfo, sizeof(wireInfo));
    }

    return queue_packet(state, NETPLAY_PACKET_LOBBY_STATE, &wireInfo, sizeof(wireInfo));
}

bool netplayQueueMatchInit(struct NetplayState *state, const struct NetplayMatchInitInfo *info)
{
    struct NetplayMatchInitWire wireInfo;

    if (state == NULL || info == NULL)
    {
        return false;
    }

    encode_match_init(info, &wireInfo);
    if (state->mode == NETPLAY_MODE_HOST)
    {
        return queue_packet_to_all_host_peers(state, NETPLAY_PACKET_MATCH_INIT, &wireInfo, sizeof(wireInfo));
    }

    return queue_packet(state, NETPLAY_PACKET_MATCH_INIT, &wireInfo, sizeof(wireInfo));
}

bool netplayQueueSnapshot(struct NetplayState *state, const unsigned char *payload, size_t payloadSize)
{
    if (state == NULL || payload == NULL || payloadSize == 0u)
    {
        return false;
    }

    if (state->mode == NETPLAY_MODE_HOST)
    {
        return queue_packet_to_all_host_peers(state, NETPLAY_PACKET_SNAPSHOT, payload, payloadSize);
    }

    return queue_packet(state, NETPLAY_PACKET_SNAPSHOT, payload, payloadSize);
}

bool netplayQueueActionRequest(struct NetplayState *state, const struct GameAction *action)
{
    struct NetplayActionWire wireAction;

    if (state == NULL || action == NULL)
    {
        return false;
    }

    memset(&wireAction, 0, sizeof(wireAction));
    encode_action(action, &wireAction);
    return queue_packet(state, NETPLAY_PACKET_ACTION_REQUEST, &wireAction, sizeof(wireAction));
}

bool netplayQueueActionResult(struct NetplayState *state,
                              const struct GameAction *action,
                              const struct GameActionResult *result,
                              uint32_t stateHash)
{
    struct NetplayActionOutcomeWire wireOutcome;

    if (state == NULL || action == NULL || result == NULL)
    {
        return false;
    }

    memset(&wireOutcome, 0, sizeof(wireOutcome));
    encode_action(action, &wireOutcome.action);
    encode_action_result(result, &wireOutcome.result);
    wireOutcome.stateHash = encode_u32(stateHash);
    if (state->mode == NETPLAY_MODE_HOST)
    {
        return queue_packet_to_all_host_peers(state, NETPLAY_PACKET_ACTION_RESULT, &wireOutcome, sizeof(wireOutcome));
    }

    return queue_packet(state, NETPLAY_PACKET_ACTION_RESULT, &wireOutcome, sizeof(wireOutcome));
}

bool netplayQueueActionReject(struct NetplayState *state, const char *message, uint32_t stateHash)
{
    return netplayQueueActionRejectForHostPeer(state, -1, message, stateHash);
}

bool netplayQueueActionRejectForHostPeer(struct NetplayState *state,
                                         int peerId,
                                         const char *message,
                                         uint32_t stateHash)
{
    struct NetplayRejectWire wireReject;

    if (state == NULL)
    {
        return false;
    }

    memset(&wireReject, 0, sizeof(wireReject));
    wireReject.stateHash = encode_u32(stateHash);
    snprintf(wireReject.message, sizeof(wireReject.message), "%s", message == NULL ? "" : message);
    if (state->mode == NETPLAY_MODE_HOST)
    {
        if (peerId < 0)
        {
            return queue_packet_to_all_host_peers(state, NETPLAY_PACKET_ACTION_REJECT, &wireReject, sizeof(wireReject));
        }

        return queue_packet_to_peer(state, peerId, NETPLAY_PACKET_ACTION_REJECT, &wireReject, sizeof(wireReject));
    }

    return queue_packet(state, NETPLAY_PACKET_ACTION_REJECT, &wireReject, sizeof(wireReject));
}

bool netplayQueueTradeOffer(struct NetplayState *state, const struct GameAction *offerAction)
{
    return netplayQueueTradeOfferForHostPeer(state, 0, offerAction);
}

bool netplayQueueTradeOfferForHostPeer(struct NetplayState *state,
                                       int peerId,
                                       const struct GameAction *offerAction)
{
    struct NetplayActionWire wireAction;

    if (state == NULL || offerAction == NULL)
    {
        return false;
    }

    memset(&wireAction, 0, sizeof(wireAction));
    encode_action(offerAction, &wireAction);

    if (state->mode == NETPLAY_MODE_HOST)
    {
        return queue_packet_to_peer(state, peerId, NETPLAY_PACKET_TRADE_OFFER, &wireAction, sizeof(wireAction));
    }

    return queue_packet(state, NETPLAY_PACKET_TRADE_OFFER, &wireAction, sizeof(wireAction));
}

bool netplayQueueTradeResponse(struct NetplayState *state,
                               const struct GameAction *offerAction,
                               bool accepted,
                               uint32_t stateHash)
{
    return netplayQueueTradeResponseForHostPeer(state, 0, offerAction, accepted, stateHash);
}

bool netplayQueueTradeResponseForHostPeer(struct NetplayState *state,
                                          int peerId,
                                          const struct GameAction *offerAction,
                                          bool accepted,
                                          uint32_t stateHash)
{
    struct NetplayTradeResponseWire wireResponse;

    if (state == NULL || offerAction == NULL)
    {
        return false;
    }

    memset(&wireResponse, 0, sizeof(wireResponse));
    encode_action(offerAction, &wireResponse.action);
    wireResponse.accepted = encode_bool(accepted);
    wireResponse.stateHash = encode_u32(stateHash);

    if (state->mode == NETPLAY_MODE_HOST)
    {
        return queue_packet_to_peer(state, peerId, NETPLAY_PACKET_TRADE_RESPONSE, &wireResponse, sizeof(wireResponse));
    }

    return queue_packet(state, NETPLAY_PACKET_TRADE_RESPONSE, &wireResponse, sizeof(wireResponse));
}

bool netplayQueueCapabilities(struct NetplayState *state,
                             uint32_t capabilityFlags,
                             uint32_t protocolMinVersion,
                             uint32_t protocolMaxVersion)
{
    struct NetplayCapabilitiesWire wireCapabilities;

    if (state == NULL)
    {
        return false;
    }

    memset(&wireCapabilities, 0, sizeof(wireCapabilities));
    wireCapabilities.capabilityFlags = encode_u32(capabilityFlags);
    wireCapabilities.protocolMinVersion = encode_u32(protocolMinVersion);
    wireCapabilities.protocolMaxVersion = encode_u32(protocolMaxVersion);
    if (state->mode == NETPLAY_MODE_HOST)
    {
        return queue_packet_to_all_host_peers(state, NETPLAY_PACKET_CAPABILITIES, &wireCapabilities, sizeof(wireCapabilities));
    }

    return queue_packet(state, NETPLAY_PACKET_CAPABILITIES, &wireCapabilities, sizeof(wireCapabilities));
}

bool netplayQueueResyncRequest(struct NetplayState *state, uint32_t stateHash, const char *reason)
{
    struct NetplayResyncRequestWire wireResync;

    if (state == NULL)
    {
        return false;
    }

    memset(&wireResync, 0, sizeof(wireResync));
    wireResync.stateHash = encode_u32(stateHash);
    snprintf(wireResync.reason, sizeof(wireResync.reason), "%s", reason == NULL ? "" : reason);
    return queue_packet(state, NETPLAY_PACKET_RESYNC_REQUEST, &wireResync, sizeof(wireResync));
}

enum NetplayMode netplayGetMode(const struct NetplayState *state)
{
    return state == NULL ? NETPLAY_MODE_NONE : state->mode;
}

enum NetplayConnectionState netplayGetConnectionState(const struct NetplayState *state)
{
    return state == NULL ? NETPLAY_CONNECTION_IDLE : state->connectionState;
}

bool netplayIsRelayTransport(const struct NetplayState *state)
{
    return state != NULL && state->relayTransport;
}

bool netplayIsConnected(const struct NetplayState *state)
{
    if (state == NULL || state->connectionState != NETPLAY_CONNECTION_CONNECTED)
    {
        return false;
    }

    if (state->mode == NETPLAY_MODE_HOST)
    {
        return count_tracked_host_peers(state) > 0;
    }

    return state->peerSocket != NET_INVALID_SOCKET;
}

const char *netplayGetLastError(const struct NetplayState *state)
{
    return state == NULL ? "" : state->lastError;
}

const char *netplayGetPeerAddress(const struct NetplayState *state)
{
    return state == NULL ? "" : state->peerAddress;
}

const char *netplayGetLocalAddress(const struct NetplayState *state)
{
    return state == NULL ? "" : state->localAddress;
}

unsigned short netplayGetPort(const struct NetplayState *state)
{
    return state == NULL ? 0u : state->port;
}

int netplayGetTrackedHostPeerCount(const struct NetplayState *state)
{
    if (state == NULL)
    {
        return 0;
    }

    return count_tracked_host_peers(state);
}
