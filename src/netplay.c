#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "netplay.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

enum NetplayPacketType
{
    NETPLAY_PACKET_HELLO = 1,
    NETPLAY_PACKET_SNAPSHOT = 2,
    NETPLAY_PACKET_ACTION_REQUEST = 3,
    NETPLAY_PACKET_ACTION_RESULT = 4,
    NETPLAY_PACKET_ACTION_REJECT = 5,
    NETPLAY_PACKET_TRADE_OFFER = 6,
    NETPLAY_PACKET_TRADE_RESPONSE = 7
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

struct NetplayQueuedPacket
{
    size_t length;
    size_t offset;
    unsigned char bytes[sizeof(struct NetplayPacketHeader) + NETPLAY_MAX_PAYLOAD_SIZE];
};

struct NetplayState
{
    enum NetplayMode mode;
    enum NetplayConnectionState connectionState;
    NetSocket listenSocket;
    NetSocket peerSocket;
    unsigned short port;
    char peerAddress[NETPLAY_MAX_PEER_ADDRESS + 1];
    char lastError[NETPLAY_MAX_STATUS_TEXT];
    unsigned char recvBuffer[NETPLAY_RECV_BUFFER_SIZE];
    size_t recvLength;
    struct NetplayQueuedPacket sendQueue[NETPLAY_SEND_QUEUE_CAPACITY];
    int sendHead;
    int sendCount;
    struct NetplayEvent eventQueue[NETPLAY_EVENT_QUEUE_CAPACITY];
    int eventHead;
    int eventCount;
};

#ifdef _WIN32
static bool gSocketLayerReady = false;
#endif

static bool net_socket_layer_init(void);
static bool is_loopback_host(const char *hostAddress);
static void clear_last_error(struct NetplayState *state);
static void set_last_error(struct NetplayState *state, const char *message);
static bool set_socket_nonblocking(NetSocket socketHandle);
static void set_socket_nodelay(NetSocket socketHandle);
static void close_socket_if_open(NetSocket *socketHandle);
static bool socket_is_writable(NetSocket socketHandle);
static void reset_connection_buffers(struct NetplayState *state);
static bool push_event(struct NetplayState *state, const struct NetplayEvent *event);
static bool queue_packet(struct NetplayState *state, enum NetplayPacketType type, const void *payload, size_t payloadSize);
static void flush_send_queue(struct NetplayState *state);
static void accept_pending_client(struct NetplayState *state);
static void advance_pending_connect(struct NetplayState *state);
static void read_from_peer(struct NetplayState *state);
static void parse_incoming_packets(struct NetplayState *state);
static void disconnect_peer(struct NetplayState *state, bool keepListening, const char *message);
static bool decode_packet(struct NetplayState *state, enum NetplayPacketType type, const unsigned char *payload, size_t payloadSize);
static void encode_action(const struct GameAction *action, struct NetplayActionWire *wireAction);
static void decode_action(struct GameAction *action, const struct NetplayActionWire *wireAction);
static void encode_action_result(const struct GameActionResult *result, struct NetplayActionResultWire *wireResult);
static void decode_action_result(struct GameActionResult *result, const struct NetplayActionResultWire *wireResult);
static uint32_t encode_u32(uint32_t value);
static uint32_t encode_i32(int value);
static uint32_t encode_bool(bool value);
static uint32_t decode_u32(uint32_t value);
static int decode_i32(uint32_t value);
static bool decode_bool(uint32_t value);

static uint32_t encode_u32(uint32_t value)
{
    return htonl(value);
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

static bool queue_packet(struct NetplayState *state, enum NetplayPacketType type, const void *payload, size_t payloadSize)
{
    struct NetplayQueuedPacket *packet = NULL;
    struct NetplayPacketHeader header;

    if (state == NULL || payloadSize > NETPLAY_MAX_PAYLOAD_SIZE || state->sendCount >= NETPLAY_SEND_QUEUE_CAPACITY)
    {
        return false;
    }

    {
        const int writeIndex = (state->sendHead + state->sendCount) % NETPLAY_SEND_QUEUE_CAPACITY;
        packet = &state->sendQueue[writeIndex];
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

static void disconnect_peer(struct NetplayState *state, bool keepListening, const char *message)
{
    struct NetplayEvent event;

    if (state == NULL)
    {
        return;
    }

    close_socket_if_open(&state->peerSocket);
    reset_connection_buffers(state);
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

static void flush_send_queue(struct NetplayState *state)
{
    while (state != NULL &&
           state->peerSocket != NET_INVALID_SOCKET &&
           state->connectionState == NETPLAY_CONNECTION_CONNECTED &&
           state->sendCount > 0)
    {
        struct NetplayQueuedPacket *packet = &state->sendQueue[state->sendHead];
        const int remaining = (int)(packet->length - packet->offset);
        const int sent = send(state->peerSocket,
                              (const char *)(packet->bytes + packet->offset),
                              remaining,
                              0);
        if (sent == NET_SOCKET_ERROR)
        {
            const int errorCode = net_last_error_code();
            if (NET_WOULD_BLOCK(errorCode))
            {
                return;
            }

            disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "send failed");
            return;
        }

        if (sent <= 0)
        {
            disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "peer closed");
            return;
        }

        packet->offset += (size_t)sent;
        if (packet->offset >= packet->length)
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

    if (state == NULL || state->mode != NETPLAY_MODE_HOST || state->listenSocket == NET_INVALID_SOCKET || state->peerSocket != NET_INVALID_SOCKET)
    {
        return;
    }

    accepted = accept(state->listenSocket, (struct sockaddr *)&address, &addressLength);
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
    state->peerSocket = accepted;
    state->connectionState = NETPLAY_CONNECTION_CONNECTED;
    snprintf(state->peerAddress, sizeof(state->peerAddress), "%s", inet_ntoa(address.sin_addr));
    clear_last_error(state);

    {
        struct NetplayEvent event;
        memset(&event, 0, sizeof(event));
        event.type = NETPLAY_EVENT_CLIENT_CONNECTED;
        push_event(state, &event);
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
        state->connectionState = NETPLAY_CONNECTION_ERROR;
        return;
    }

    state->connectionState = NETPLAY_CONNECTION_CONNECTED;
    clear_last_error(state);
    {
        struct NetplayEvent event;
        memset(&event, 0, sizeof(event));
        event.type = NETPLAY_EVENT_CONNECTED;
        push_event(state, &event);
    }
}

static void read_from_peer(struct NetplayState *state)
{
    while (state != NULL &&
           state->peerSocket != NET_INVALID_SOCKET &&
           state->connectionState == NETPLAY_CONNECTION_CONNECTED)
    {
        const int remainingCapacity = (int)(sizeof(state->recvBuffer) - state->recvLength);
        const int received = recv(state->peerSocket, (char *)(state->recvBuffer + state->recvLength), remainingCapacity, 0);

        if (received == 0)
        {
            disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "peer disconnected");
            return;
        }

        if (received == NET_SOCKET_ERROR)
        {
            const int errorCode = net_last_error_code();
            if (NET_WOULD_BLOCK(errorCode))
            {
                return;
            }

            disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "receive failed");
            return;
        }

        if (received < 0)
        {
            return;
        }

        state->recvLength += (size_t)received;
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

static bool decode_packet(struct NetplayState *state, enum NetplayPacketType type, const unsigned char *payload, size_t payloadSize)
{
    struct NetplayEvent event;

    if (state == NULL || payload == NULL)
    {
        return false;
    }

    memset(&event, 0, sizeof(event));
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
            event.accepted = decode_bool(wireResponse.accepted);
            event.stateHash = decode_u32(wireResponse.stateHash);
        }
        break;

    default:
        return false;
    }

    return push_event(state, &event);
}

static void parse_incoming_packets(struct NetplayState *state)
{
    while (state != NULL &&
           state->recvLength >= sizeof(struct NetplayPacketHeader) &&
           state->eventCount < NETPLAY_EVENT_QUEUE_CAPACITY)
    {
        struct NetplayPacketHeader header;
        size_t payloadSize = 0u;
        enum NetplayPacketType type = NETPLAY_PACKET_HELLO;

        memcpy(&header, state->recvBuffer, sizeof(header));
        payloadSize = (size_t)decode_u32(header.payloadSize);
        type = (enum NetplayPacketType)decode_u32(header.type);
        if (memcmp(header.magic, NETPLAY_PACKET_MAGIC, sizeof(header.magic)) != 0 ||
            decode_u32(header.version) != NETPLAY_PROTOCOL_VERSION ||
            payloadSize > NETPLAY_MAX_PAYLOAD_SIZE)
        {
            disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "invalid packet");
            return;
        }

        {
            const size_t fullPacketLength = sizeof(header) + payloadSize;
            const unsigned char *payload = NULL;
            if (state->recvLength < fullPacketLength)
            {
                return;
            }

            payload = state->recvBuffer + sizeof(header);
            if (!decode_packet(state, type, payload, payloadSize))
            {
                disconnect_peer(state, state->mode == NETPLAY_MODE_HOST, "packet decode failed");
                return;
            }

            memmove(state->recvBuffer, state->recvBuffer + fullPacketLength, state->recvLength - fullPacketLength);
            state->recvLength -= fullPacketLength;
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
    state->connectionState = NETPLAY_CONNECTION_IDLE;
    return state;
}

void netplayDestroy(struct NetplayState *state)
{
    if (state == NULL)
    {
        return;
    }

    close_socket_if_open(&state->peerSocket);
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
    close_socket_if_open(&state->listenSocket);
    reset_connection_buffers(state);
    state->eventHead = 0;
    state->eventCount = 0;
    state->mode = NETPLAY_MODE_HOST;
    state->port = port;
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

    if (listen(state->listenSocket, 1) == NET_SOCKET_ERROR || !set_socket_nonblocking(state->listenSocket))
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

bool netplayStartClient(struct NetplayState *state, const char *hostAddress, unsigned short port)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *current = NULL;
    char portText[16];
    int lookupResult = 0;

    if (state == NULL || hostAddress == NULL || hostAddress[0] == '\0')
    {
        return false;
    }

    close_socket_if_open(&state->peerSocket);
    close_socket_if_open(&state->listenSocket);
    reset_connection_buffers(state);
    state->eventHead = 0;
    state->eventCount = 0;
    state->mode = NETPLAY_MODE_CLIENT;
    state->port = port;
    snprintf(state->peerAddress, sizeof(state->peerAddress), "%s", hostAddress);
    snprintf(portText, sizeof(portText), "%hu", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    lookupResult = getaddrinfo(hostAddress, portText, &hints, &results);
    if (lookupResult != 0 || results == NULL)
    {
        if (is_loopback_host(hostAddress))
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
            clear_last_error(state);
            {
                struct NetplayEvent event;
                memset(&event, 0, sizeof(event));
                event.type = NETPLAY_EVENT_CONNECTED;
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
                clear_last_error(state);
                freeaddrinfo(results);
                return true;
            }
        }

        close_socket_if_open(&state->peerSocket);
    }

    freeaddrinfo(results);
    if (is_loopback_host(hostAddress))
    {
        set_last_error(state, "connect failed (use host LAN IP)");
    }
    else
    {
        set_last_error(state, "connect failed");
    }
    state->connectionState = NETPLAY_CONNECTION_ERROR;
    return false;
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
    parse_incoming_packets(state);
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

    return queue_packet(state, NETPLAY_PACKET_HELLO, &wireHello, sizeof(wireHello));
}

bool netplayQueueSnapshot(struct NetplayState *state, const unsigned char *payload, size_t payloadSize)
{
    if (state == NULL || payload == NULL || payloadSize == 0u)
    {
        return false;
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
    return queue_packet(state, NETPLAY_PACKET_ACTION_RESULT, &wireOutcome, sizeof(wireOutcome));
}

bool netplayQueueActionReject(struct NetplayState *state, const char *message, uint32_t stateHash)
{
    struct NetplayRejectWire wireReject;

    if (state == NULL)
    {
        return false;
    }

    memset(&wireReject, 0, sizeof(wireReject));
    wireReject.stateHash = encode_u32(stateHash);
    snprintf(wireReject.message, sizeof(wireReject.message), "%s", message == NULL ? "" : message);
    return queue_packet(state, NETPLAY_PACKET_ACTION_REJECT, &wireReject, sizeof(wireReject));
}

bool netplayQueueTradeOffer(struct NetplayState *state, const struct GameAction *offerAction)
{
    struct NetplayActionWire wireAction;

    if (state == NULL || offerAction == NULL)
    {
        return false;
    }

    memset(&wireAction, 0, sizeof(wireAction));
    encode_action(offerAction, &wireAction);
    return queue_packet(state, NETPLAY_PACKET_TRADE_OFFER, &wireAction, sizeof(wireAction));
}

bool netplayQueueTradeResponse(struct NetplayState *state,
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
    return queue_packet(state, NETPLAY_PACKET_TRADE_RESPONSE, &wireResponse, sizeof(wireResponse));
}

enum NetplayMode netplayGetMode(const struct NetplayState *state)
{
    return state == NULL ? NETPLAY_MODE_NONE : state->mode;
}

enum NetplayConnectionState netplayGetConnectionState(const struct NetplayState *state)
{
    return state == NULL ? NETPLAY_CONNECTION_IDLE : state->connectionState;
}

bool netplayIsConnected(const struct NetplayState *state)
{
    return state != NULL && state->connectionState == NETPLAY_CONNECTION_CONNECTED && state->peerSocket != NET_INVALID_SOCKET;
}

const char *netplayGetLastError(const struct NetplayState *state)
{
    return state == NULL ? "" : state->lastError;
}

const char *netplayGetPeerAddress(const struct NetplayState *state)
{
    return state == NULL ? "" : state->peerAddress;
}

unsigned short netplayGetPort(const struct NetplayState *state)
{
    return state == NULL ? 0u : state->port;
}
