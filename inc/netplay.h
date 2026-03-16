#ifndef NETPLAY_H
#define NETPLAY_H

#include "game_action.h"
#include "player.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETPLAY_PROTOCOL_VERSION 1u
#define NETPLAY_DEFAULT_PORT 24680u
#define NETPLAY_MAX_PAYLOAD_SIZE 65536u
#define NETPLAY_MAX_STATUS_TEXT 96
#define NETPLAY_MAX_PEER_ADDRESS 63

enum NetplayMode
{
    NETPLAY_MODE_NONE,
    NETPLAY_MODE_HOST,
    NETPLAY_MODE_CLIENT
};

enum NetplayConnectionState
{
    NETPLAY_CONNECTION_IDLE,
    NETPLAY_CONNECTION_LISTENING,
    NETPLAY_CONNECTION_CONNECTING,
    NETPLAY_CONNECTION_CONNECTED,
    NETPLAY_CONNECTION_DISCONNECTED,
    NETPLAY_CONNECTION_ERROR
};

enum NetplayEventType
{
    NETPLAY_EVENT_NONE,
    NETPLAY_EVENT_CLIENT_CONNECTED,
    NETPLAY_EVENT_CONNECTED,
    NETPLAY_EVENT_DISCONNECTED,
    NETPLAY_EVENT_HELLO,
    NETPLAY_EVENT_SNAPSHOT,
    NETPLAY_EVENT_ACTION_REQUEST,
    NETPLAY_EVENT_ACTION_RESULT,
    NETPLAY_EVENT_ACTION_REJECT,
    NETPLAY_EVENT_TRADE_OFFER,
    NETPLAY_EVENT_TRADE_RESPONSE
};

struct NetplayHelloInfo
{
    enum PlayerType assignedPlayer;
    enum PlayerType hostPlayer;
    int seatAuthority[MAX_PLAYERS];
};

struct NetplayEvent
{
    enum NetplayEventType type;
    struct NetplayHelloInfo hello;
    struct GameAction action;
    struct GameActionResult result;
    bool accepted;
    uint32_t stateHash;
    char message[NETPLAY_MAX_STATUS_TEXT];
    size_t payloadSize;
    unsigned char payload[NETPLAY_MAX_PAYLOAD_SIZE];
};

struct NetplayState;

struct NetplayState *netplayCreate(void);
void netplayDestroy(struct NetplayState *state);

bool netplayStartHost(struct NetplayState *state, unsigned short port);
bool netplayStartClient(struct NetplayState *state, const char *hostAddress, unsigned short port);
void netplayUpdate(struct NetplayState *state);
bool netplayPollEvent(struct NetplayState *state, struct NetplayEvent *event);

bool netplayQueueHello(struct NetplayState *state,
                       enum PlayerType assignedPlayer,
                       enum PlayerType hostPlayer,
                       const int seatAuthority[MAX_PLAYERS]);
bool netplayQueueSnapshot(struct NetplayState *state, const unsigned char *payload, size_t payloadSize);
bool netplayQueueActionRequest(struct NetplayState *state, const struct GameAction *action);
bool netplayQueueActionResult(struct NetplayState *state,
                              const struct GameAction *action,
                              const struct GameActionResult *result,
                              uint32_t stateHash);
bool netplayQueueActionReject(struct NetplayState *state, const char *message, uint32_t stateHash);
bool netplayQueueTradeOffer(struct NetplayState *state, const struct GameAction *offerAction);
bool netplayQueueTradeResponse(struct NetplayState *state,
                               const struct GameAction *offerAction,
                               bool accepted,
                               uint32_t stateHash);

enum NetplayMode netplayGetMode(const struct NetplayState *state);
enum NetplayConnectionState netplayGetConnectionState(const struct NetplayState *state);
bool netplayIsConnected(const struct NetplayState *state);
const char *netplayGetLastError(const struct NetplayState *state);
const char *netplayGetPeerAddress(const struct NetplayState *state);
const char *netplayGetLocalAddress(const struct NetplayState *state);
unsigned short netplayGetPort(const struct NetplayState *state);

#endif
