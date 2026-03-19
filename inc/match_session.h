#ifndef MATCH_SESSION_H
#define MATCH_SESSION_H

#include "game_action.h"
#include "map.h"

#include <stdbool.h>
#include <stdint.h>

struct GameActionContext;
struct GameActionResult;
struct NetplayState;

enum MatchSeatAuthority
{
    MATCH_SEAT_LOCAL,
    MATCH_SEAT_REMOTE,
    MATCH_SEAT_AI
};

enum MatchNetworkMode
{
    MATCH_NETWORK_NONE,
    MATCH_NETWORK_PRIVATE_HOST,
    MATCH_NETWORK_PRIVATE_CLIENT
};

enum MatchConnectionStatus
{
    MATCH_CONNECTION_LOCAL,
    MATCH_CONNECTION_WAITING_FOR_PLAYER,
    MATCH_CONNECTION_CONNECTING,
    MATCH_CONNECTION_SYNCING,
    MATCH_CONNECTION_CONNECTED,
    MATCH_CONNECTION_DISCONNECTED,
    MATCH_CONNECTION_ERROR
};

struct MatchSession
{
    struct Map map;
    bool isHost;
    enum PlayerType localPlayer;
    enum MatchSeatAuthority seatAuthority[MAX_PLAYERS];
    uint32_t stateHash;
    enum MatchNetworkMode networkMode;
    enum MatchConnectionStatus connectionStatus;
    bool ready;
    bool matchStarted;
    bool pendingUiResetForMatchInit;
    bool awaitingAuthoritativeUpdate;
    bool initialSnapshotReceived;
    bool pendingTradeOfferActive;
    bool pendingTradeAwaitingLocalResponse;
    bool pendingTradeRequestedByRemote;
    struct GameAction pendingTradeOffer;
    int pendingTradePeerId;
    struct NetplayState *netplay;
    char connectionError[96];
    char reconnectHost[64];
    unsigned short reconnectPort;
    double reconnectAttemptAtSeconds;
    double nextResyncRequestAtSeconds;
    int reconnectAttempts;
    bool reconnectEnabled;
    bool reconnectNotified;
    uint32_t peerCapabilityFlags;
    uint32_t peerProtocolMinVersion;
    uint32_t peerProtocolMaxVersion;
    bool peerCapabilitiesReceived;
    int remotePeerForPlayer[MAX_PLAYERS];
};

void matchSessionInit(struct MatchSession *session);
void matchSessionShutdown(struct MatchSession *session);
void matchSessionConfigureHotseat(struct MatchSession *session);
void matchSessionConfigureSinglePlayer(struct MatchSession *session, enum PlayerType localPlayer);
void matchSessionConfigurePrivateHost(struct MatchSession *session, enum PlayerType localPlayer);
void matchSessionConfigurePrivateClient(struct MatchSession *session, enum PlayerType localPlayer);
bool matchSessionOpenPrivateHost(struct MatchSession *session, unsigned short port);
bool matchSessionOpenPrivateClient(struct MatchSession *session, const char *hostAddress, unsigned short port);
void matchSessionUpdate(struct MatchSession *session);
void matchSessionSetSeatAuthority(struct MatchSession *session, enum PlayerType player, enum MatchSeatAuthority authority);
enum MatchSeatAuthority matchSessionGetSeatAuthority(const struct MatchSession *session, enum PlayerType player);
enum PlayerType matchSessionGetLocalPlayer(const struct MatchSession *session);
bool matchSessionLocalControlsPlayer(const struct MatchSession *session, enum PlayerType player);
bool matchSessionLocalCanActOnCurrentDecision(const struct MatchSession *session);
bool matchSessionSubmitAction(struct MatchSession *session,
                              const struct GameAction *action,
                              const struct GameActionContext *context,
                              struct GameActionResult *result);
bool matchSessionBroadcastState(struct MatchSession *session);
bool matchSessionHasPendingTradeOfferForLocalResponse(const struct MatchSession *session);
bool matchSessionGetPendingTradeOffer(const struct MatchSession *session, struct GameAction *offerAction);
bool matchSessionRespondToPendingTradeOffer(struct MatchSession *session, bool accept);
bool matchSessionIsNetplay(const struct MatchSession *session);
bool matchSessionIsReady(const struct MatchSession *session);
bool matchSessionHasStarted(const struct MatchSession *session);
bool matchSessionCanStartNetplayMatch(const struct MatchSession *session);
bool matchSessionStartNetplayMatch(struct MatchSession *session);
bool matchSessionRestartNetplayMatch(struct MatchSession *session);
bool matchSessionHostToggleNetplayLobbySeat(struct MatchSession *session, enum PlayerType player);
bool matchSessionHostCycleNetplayLobbyAiDifficulty(struct MatchSession *session);
bool matchSessionConsumePendingMatchInitUiReset(struct MatchSession *session);
bool matchSessionShouldAnimateLocalRoll(const struct MatchSession *session);
bool matchSessionShouldRunAi(const struct MatchSession *session);
bool matchSessionIsHost(const struct MatchSession *session);
enum MatchConnectionStatus matchSessionGetConnectionStatus(const struct MatchSession *session);
const char *matchSessionGetConnectionError(const struct MatchSession *session);
void matchSessionRefreshStateHash(struct MatchSession *session);
uint32_t matchSessionGetStateHash(const struct MatchSession *session);
void matchSessionSetActive(struct MatchSession *session);
const struct MatchSession *matchSessionGetActive(void);
struct MatchSession *matchSessionGetActiveMutable(void);

#endif
