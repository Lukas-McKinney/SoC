#include "match_session.h"

#include "game_action.h"
#include "game_logic.h"
#include "localization.h"
#include "map_snapshot.h"
#include "netplay.h"
#include "debug_log.h"
#include "renderer_ui.h"
#include "ui_state.h"

#include <stdio.h>
#include <string.h>

#include <raylib.h>

static struct MatchSession *gActiveSession = NULL;

static enum PlayerType active_decision_player(const struct MatchSession *session);
static void clear_connection_error(struct MatchSession *session);
static void set_connection_error(struct MatchSession *session, const char *message);
static bool configure_default_private_host_seats(struct MatchSession *session);
static bool is_authoritative_only_action(enum GameActionType type);
static bool active_decision_is_remote(const struct MatchSession *session);
static void build_lobby_state_info(const struct MatchSession *session, struct NetplayLobbyStateInfo *info);
static void apply_lobby_state_info(struct MatchSession *session, const struct NetplayLobbyStateInfo *info);
static bool build_match_init_info(const struct MatchSession *session, struct NetplayMatchInitInfo *info);
static bool apply_match_init_info(struct MatchSession *session, const struct NetplayMatchInitInfo *info);
static bool send_match_init(struct MatchSession *session);
static bool submit_client_action(struct MatchSession *session,
                                 const struct GameAction *action,
                                 const struct GameActionContext *context,
                                 struct GameActionResult *result);
static bool apply_authoritative_action_to_client_map(struct MatchSession *session,
                                                     const struct GameAction *action,
                                                     const struct GameActionResult *result);
static void apply_client_authoritative_result(struct MatchSession *session,
                                              const struct GameAction *action,
                                              const struct GameActionResult *result,
                                              uint32_t authoritativeStateHash);
static void init_action_result(struct GameActionResult *result);
static enum DevelopmentCardType development_card_for_action(enum GameActionType type);
static bool action_actor_is_local_viewer(const struct MatchSession *session, enum PlayerType actor);
static void show_action_feedback(const struct MatchSession *session,
                                 enum PlayerType actor,
                                 const struct GameAction *action,
                                 const struct GameActionResult *result);
static bool broadcast_host_snapshot(struct MatchSession *session);
static void handle_netplay_event(struct MatchSession *session, const struct NetplayEvent *event);
static void reset_client_transient_ui(void);
static void clear_pending_trade_offer(struct MatchSession *session);
static bool actions_match(const struct GameAction *a, const struct GameAction *b);
static void reset_reconnect_state(struct MatchSession *session);
static void schedule_client_reconnect(struct MatchSession *session, double delaySeconds);
static void attempt_client_reconnect(struct MatchSession *session);
static bool queue_local_capabilities(struct MatchSession *session);
static void request_snapshot_resync(struct MatchSession *session, const char *reason);
static void reset_remote_peer_assignments(struct MatchSession *session);
static void clear_remote_peer_assignment_for_peer(struct MatchSession *session, int peerId);
static enum PlayerType find_assigned_remote_player_for_peer(const struct MatchSession *session, int peerId);
static enum PlayerType assign_remote_player_to_peer(struct MatchSession *session, int peerId);
static bool send_hello_to_peer(struct MatchSession *session, int peerId, enum PlayerType assignedPlayer);
static bool send_lobby_state_to_peer(struct MatchSession *session, int peerId);
static bool broadcast_lobby_state(struct MatchSession *session);

#define CLIENT_RECONNECT_DELAY_SECONDS 2.5
#define CLIENT_RECONNECT_MAX_ATTEMPTS 20
#define CLIENT_RESYNC_REQUEST_COOLDOWN_SECONDS 1.0
#define MATCH_PROTOCOL_MIN_VERSION NETPLAY_PROTOCOL_VERSION
#define MATCH_PROTOCOL_MAX_VERSION NETPLAY_PROTOCOL_VERSION
#define MATCH_CAPABILITY_FLAGS (NETPLAY_CAPABILITY_HEARTBEAT | NETPLAY_CAPABILITY_SNAPSHOT_RESYNC | NETPLAY_CAPABILITY_ACTION_HASH_SYNC)

static void clear_pending_trade_offer(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    session->pendingTradeOfferActive = false;
    session->pendingTradeAwaitingLocalResponse = false;
    session->pendingTradeRequestedByRemote = false;
    session->pendingTradePeerId = -1;
    memset(&session->pendingTradeOffer, 0, sizeof(session->pendingTradeOffer));
}

static bool queue_local_capabilities(struct MatchSession *session)
{
    if (session == NULL || session->netplay == NULL || !netplayIsConnected(session->netplay))
    {
        return false;
    }

    return netplayQueueCapabilities(session->netplay,
                                    MATCH_CAPABILITY_FLAGS,
                                    MATCH_PROTOCOL_MIN_VERSION,
                                    MATCH_PROTOCOL_MAX_VERSION);
}

static void request_snapshot_resync(struct MatchSession *session, const char *reason)
{
    const double nowSeconds = GetTime();

    if (session == NULL ||
        session->isHost ||
        session->netplay == NULL ||
        !netplayIsConnected(session->netplay) ||
        !session->peerCapabilitiesReceived ||
        (session->peerCapabilityFlags & NETPLAY_CAPABILITY_SNAPSHOT_RESYNC) == 0u)
    {
        return;
    }

    if (session->nextResyncRequestAtSeconds > 0.0 && nowSeconds < session->nextResyncRequestAtSeconds)
    {
        return;
    }

    session->nextResyncRequestAtSeconds = nowSeconds + CLIENT_RESYNC_REQUEST_COOLDOWN_SECONDS;
    if (netplayQueueResyncRequest(session->netplay, session->stateHash, reason))
    {
        debugLog("NET", "requested snapshot resync: %s (localHash=%u)", reason == NULL ? "" : reason, session->stateHash);
    }
}

static void reset_reconnect_state(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    session->reconnectAttempts = 0;
    session->reconnectAttemptAtSeconds = -1.0;
    session->nextResyncRequestAtSeconds = -1.0;
    session->reconnectNotified = false;
}

static void schedule_client_reconnect(struct MatchSession *session, double delaySeconds)
{
    if (session == NULL ||
        session->networkMode != MATCH_NETWORK_PRIVATE_CLIENT ||
        !session->reconnectEnabled ||
        session->reconnectHost[0] == '\0' ||
        session->reconnectPort == 0u)
    {
        return;
    }

    if (session->reconnectAttempts >= CLIENT_RECONNECT_MAX_ATTEMPTS)
    {
        set_connection_error(session, "reconnect attempts exhausted");
        session->connectionStatus = MATCH_CONNECTION_ERROR;
        return;
    }

    session->reconnectAttemptAtSeconds = GetTime() + (delaySeconds < 0.0 ? 0.0 : delaySeconds);
}

static void attempt_client_reconnect(struct MatchSession *session)
{
    if (session == NULL ||
        session->networkMode != MATCH_NETWORK_PRIVATE_CLIENT ||
        !session->reconnectEnabled ||
        session->netplay == NULL ||
        session->reconnectHost[0] == '\0' ||
        session->reconnectPort == 0u)
    {
        return;
    }

    if (session->connectionStatus != MATCH_CONNECTION_DISCONNECTED &&
        session->connectionStatus != MATCH_CONNECTION_ERROR &&
        session->connectionStatus != MATCH_CONNECTION_CONNECTING)
    {
        return;
    }

    if (session->reconnectAttemptAtSeconds < 0.0 || GetTime() < session->reconnectAttemptAtSeconds)
    {
        return;
    }

    if (session->reconnectAttempts >= CLIENT_RECONNECT_MAX_ATTEMPTS)
    {
        return;
    }

    session->reconnectAttempts++;
    session->connectionStatus = MATCH_CONNECTION_CONNECTING;
    debugLog("NET", "client reconnect attempt %d to %s:%u",
             session->reconnectAttempts,
             session->reconnectHost,
             (unsigned int)session->reconnectPort);

    if (netplayStartClient(session->netplay, session->reconnectHost, session->reconnectPort))
    {
        session->connectionStatus = netplayGetConnectionState(session->netplay) == NETPLAY_CONNECTION_CONNECTED
                                        ? MATCH_CONNECTION_SYNCING
                                        : MATCH_CONNECTION_CONNECTING;
        clear_connection_error(session);
        session->reconnectAttemptAtSeconds = -1.0;
        if (!session->reconnectNotified)
        {
            uiShowCenteredStatus(loc("Reconnecting to host..."), UI_NOTIFICATION_NEUTRAL);
            session->reconnectNotified = true;
        }
    }
    else
    {
        set_connection_error(session, netplayGetLastError(session->netplay));
        session->connectionStatus = MATCH_CONNECTION_DISCONNECTED;
        schedule_client_reconnect(session, CLIENT_RECONNECT_DELAY_SECONDS);
    }
}

static bool actions_match(const struct GameAction *a, const struct GameAction *b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    return a->type == b->type &&
           a->tileId == b->tileId &&
           a->cornerIndex == b->cornerIndex &&
           a->sideIndex == b->sideIndex &&
           a->diceRoll == b->diceRoll &&
           a->player == b->player &&
           a->resourceA == b->resourceA &&
           a->resourceB == b->resourceB &&
           a->amountA == b->amountA &&
           a->amountB == b->amountB;
}

static void build_lobby_state_info(const struct MatchSession *session, struct NetplayLobbyStateInfo *info)
{
    if (session == NULL || info == NULL)
    {
        return;
    }

    memset(info, 0, sizeof(*info));
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        info->controlMode[player] = session->map.players[player].controlMode;
        info->aiDifficulty[player] = session->map.players[player].aiDifficulty;
    }
}

static void apply_lobby_state_info(struct MatchSession *session, const struct NetplayLobbyStateInfo *info)
{
    if (session == NULL || info == NULL)
    {
        return;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->map.players[player].controlMode = info->controlMode[player];
        session->map.players[player].aiDifficulty = info->aiDifficulty[player];
    }
}

static bool build_match_init_info(const struct MatchSession *session, struct NetplayMatchInitInfo *info)
{
    if (session == NULL || info == NULL)
    {
        return false;
    }

    memset(info, 0, sizeof(*info));
    if (!mapCreateRandomSetupConfig(&info->setup))
    {
        return false;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        info->controlMode[player] = session->map.players[player].controlMode;
        info->aiDifficulty[player] = session->map.players[player].aiDifficulty;
    }
    return true;
}

static bool apply_match_init_info(struct MatchSession *session, const struct NetplayMatchInitInfo *info)
{
    if (session == NULL || info == NULL || !setupMapFromConfig(&session->map, &info->setup))
    {
        return false;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->map.players[player].controlMode = info->controlMode[player];
        session->map.players[player].aiDifficulty = info->aiDifficulty[player];
    }

    matchSessionRefreshStateHash(session);
    return true;
}

static void reset_remote_peer_assignments(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->remotePeerForPlayer[player] = -1;
    }
}

static void clear_remote_peer_assignment_for_peer(struct MatchSession *session, int peerId)
{
    if (session == NULL || peerId < 0)
    {
        return;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (session->remotePeerForPlayer[player] == peerId)
        {
            session->remotePeerForPlayer[player] = -1;
            if (session->isHost &&
                session->networkMode == MATCH_NETWORK_PRIVATE_HOST &&
                session->seatAuthority[player] == MATCH_SEAT_REMOTE)
            {
                session->seatAuthority[player] = MATCH_SEAT_AI;
                session->map.players[player].controlMode = PLAYER_CONTROL_AI;
            }
        }
    }
}

static enum PlayerType find_assigned_remote_player_for_peer(const struct MatchSession *session, int peerId)
{
    if (session == NULL || peerId < 0)
    {
        return PLAYER_NONE;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (session->remotePeerForPlayer[player] == peerId)
        {
            return (enum PlayerType)player;
        }
    }

    return PLAYER_NONE;
}

static enum PlayerType assign_remote_player_to_peer(struct MatchSession *session, int peerId)
{
    enum PlayerType alreadyAssigned = PLAYER_NONE;

    if (session == NULL || peerId < 0)
    {
        return PLAYER_NONE;
    }

    alreadyAssigned = find_assigned_remote_player_for_peer(session, peerId);
    if (alreadyAssigned != PLAYER_NONE)
    {
        return alreadyAssigned;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (session->seatAuthority[player] == MATCH_SEAT_REMOTE && session->remotePeerForPlayer[player] < 0)
        {
            session->remotePeerForPlayer[player] = peerId;
            session->map.players[player].controlMode = PLAYER_CONTROL_HUMAN;
            return (enum PlayerType)player;
        }
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (session->seatAuthority[player] == MATCH_SEAT_AI)
        {
            session->seatAuthority[player] = MATCH_SEAT_REMOTE;
            session->remotePeerForPlayer[player] = peerId;
            session->map.players[player].controlMode = PLAYER_CONTROL_HUMAN;
            session->map.players[player].aiDifficulty = AI_DIFFICULTY_EASY;
            return (enum PlayerType)player;
        }
    }

    return PLAYER_NONE;
}

static bool send_hello_to_peer(struct MatchSession *session, int peerId, enum PlayerType assignedPlayer)
{
    int authorities[MAX_PLAYERS] = {0};

    if (session == NULL || session->netplay == NULL)
    {
        return false;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        authorities[player] = session->seatAuthority[player];
    }

    return netplayQueueHelloForHostPeer(session->netplay,
                                        peerId,
                                        assignedPlayer,
                                        session->localPlayer,
                                        authorities);
}

static bool send_lobby_state_to_peer(struct MatchSession *session, int peerId)
{
    struct NetplayLobbyStateInfo info;

    if (session == NULL || session->netplay == NULL || !netplayIsConnected(session->netplay))
    {
        return false;
    }

    build_lobby_state_info(session, &info);
    return netplayQueueLobbyStateForHostPeer(session->netplay, peerId, &info);
}

static bool broadcast_lobby_state(struct MatchSession *session)
{
    return send_lobby_state_to_peer(session, -1);
}

static bool send_match_init(struct MatchSession *session)
{
    struct NetplayMatchInitInfo info;

    if (session == NULL || session->netplay == NULL || !netplayIsConnected(session->netplay))
    {
        return false;
    }

    if (!build_match_init_info(session, &info))
    {
        return false;
    }

    if (!netplayQueueMatchInit(session->netplay, &info))
    {
        return false;
    }

    if (!apply_match_init_info(session, &info))
    {
        return false;
    }

    session->matchStarted = true;
    session->ready = true;
    session->pendingUiResetForMatchInit = false;
    session->awaitingAuthoritativeUpdate = false;
    clear_pending_trade_offer(session);
    return true;
}

static void init_action_result(struct GameActionResult *result)
{
    if (result == NULL)
    {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->drawnCard = DEVELOPMENT_CARD_COUNT;
    result->stolenResource = RESOURCE_WOOD;
}

static enum DevelopmentCardType development_card_for_action(enum GameActionType type)
{
    switch (type)
    {
    case GAME_ACTION_PLAY_KNIGHT:
        return DEVELOPMENT_CARD_KNIGHT;
    case GAME_ACTION_PLAY_ROAD_BUILDING:
        return DEVELOPMENT_CARD_ROAD_BUILDING;
    case GAME_ACTION_PLAY_YEAR_OF_PLENTY:
        return DEVELOPMENT_CARD_YEAR_OF_PLENTY;
    case GAME_ACTION_PLAY_MONOPOLY:
        return DEVELOPMENT_CARD_MONOPOLY;
    default:
        return DEVELOPMENT_CARD_COUNT;
    }
}

static bool action_actor_is_local_viewer(const struct MatchSession *session, enum PlayerType actor)
{
    if (session == NULL || actor < PLAYER_RED || actor > PLAYER_BLACK)
    {
        return false;
    }

    if (session->map.players[actor].controlMode == PLAYER_CONTROL_AI)
    {
        return false;
    }

    return session->seatAuthority[actor] == MATCH_SEAT_LOCAL;
}

static void show_action_feedback(const struct MatchSession *session,
                                 enum PlayerType actor,
                                 const struct GameAction *action,
                                 const struct GameActionResult *result)
{
    char message[160];
    const bool actorIsLocal = action_actor_is_local_viewer(session, actor);
    const char *actorLabel = actor >= PLAYER_RED && actor <= PLAYER_BLACK ? locPlayerName(actor) : loc("A player");
    const char *targetLabel = action != NULL && action->player >= PLAYER_RED && action->player <= PLAYER_BLACK
                                  ? locPlayerName(action->player)
                                  : loc("A player");
    enum DevelopmentCardType cardType = DEVELOPMENT_CARD_COUNT;
    const int rolledTotal = result != NULL && result->diceRoll >= 2 && result->diceRoll <= 12
                                ? result->diceRoll
                                : (action != NULL ? action->diceRoll : 0);

    if (action == NULL)
    {
        return;
    }

    switch (action->type)
    {
    case GAME_ACTION_ROLL_DICE:
        if (!actorIsLocal && rolledTotal >= 2 && rolledTotal <= 12)
        {
            snprintf(message, sizeof(message), loc("%s rolled %d."), actorLabel, rolledTotal);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_PLACE_ROAD:
        if (actorIsLocal)
        {
            uiShowCenteredStatus(loc("Road placed."), UI_NOTIFICATION_POSITIVE);
        }
        else
        {
            snprintf(message, sizeof(message), loc("%s built a road."), actorLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_PLACE_SETTLEMENT:
        if (actorIsLocal)
        {
            uiShowCenteredStatus(loc("Settlement placed."), UI_NOTIFICATION_POSITIVE);
        }
        else
        {
            snprintf(message, sizeof(message), loc("%s built a settlement."), actorLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_PLACE_CITY:
        if (actorIsLocal)
        {
            uiShowCenteredStatus(loc("City built."), UI_NOTIFICATION_POSITIVE);
        }
        else
        {
            snprintf(message, sizeof(message), loc("%s built a city."), actorLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_SUBMIT_DISCARD:
        if (actorIsLocal)
        {
            uiShowCenteredStatus(loc("Cards discarded."), UI_NOTIFICATION_NEUTRAL);
        }
        else
        {
            snprintf(message, sizeof(message), loc("%s discarded cards."), actorLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_MOVE_THIEF:
        if (actorIsLocal)
        {
            uiShowCenteredStatus(loc("Thief moved."), UI_NOTIFICATION_NEUTRAL);
        }
        else
        {
            snprintf(message, sizeof(message), loc("%s moved the thief."), actorLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_STEAL_RANDOM_RESOURCE:
        if (!actorIsLocal)
        {
            snprintf(message, sizeof(message), loc("%s stole from %s."), actorLabel, targetLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_BUY_DEVELOPMENT:
        if (actorIsLocal)
        {
            if (result != NULL &&
                result->drawnCard >= DEVELOPMENT_CARD_KNIGHT &&
                result->drawnCard < DEVELOPMENT_CARD_COUNT)
            {
                snprintf(message, sizeof(message), loc("Bought %s."), locDevelopmentCardTitle(result->drawnCard));
                uiShowCenteredStatus(message, UI_NOTIFICATION_POSITIVE);
            }
            else
            {
                uiShowCenteredStatus(loc("Development card bought."), UI_NOTIFICATION_POSITIVE);
            }
        }
        else
        {
            snprintf(message, sizeof(message), loc("%s bought a development card."), actorLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_PLAY_KNIGHT:
    case GAME_ACTION_PLAY_ROAD_BUILDING:
    case GAME_ACTION_PLAY_YEAR_OF_PLENTY:
    case GAME_ACTION_PLAY_MONOPOLY:
        cardType = development_card_for_action(action->type);
        if (cardType >= DEVELOPMENT_CARD_KNIGHT && cardType < DEVELOPMENT_CARD_COUNT)
        {
            if (actorIsLocal)
            {
                snprintf(message, sizeof(message), loc("Played %s."), locDevelopmentCardTitle(cardType));
                uiShowCenteredStatus(message, UI_NOTIFICATION_NEUTRAL);
            }
            else
            {
                snprintf(message, sizeof(message), loc("%s played %s."), actorLabel, locDevelopmentCardTitle(cardType));
                uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
            }
        }
        return;
    case GAME_ACTION_TRADE_MARITIME:
        if (actorIsLocal)
        {
            uiShowCenteredStatus(loc("Trade completed."), UI_NOTIFICATION_POSITIVE);
        }
        else
        {
            snprintf(message, sizeof(message), loc("%s traded with the bank."), actorLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_TRADE_WITH_PLAYER:
        if (!actorIsLocal)
        {
            snprintf(message, sizeof(message), loc("%s traded with %s."), actorLabel, targetLabel);
            uiShowCenteredStatusForPlayer(message, UI_NOTIFICATION_NEUTRAL, actor);
        }
        return;
    case GAME_ACTION_NONE:
    case GAME_ACTION_END_TURN:
    default:
        return;
    }
}

static enum PlayerType active_decision_player(const struct MatchSession *session)
{
    if (session == NULL)
    {
        return PLAYER_NONE;
    }

    if (gameHasPendingDiscards(&session->map))
    {
        return gameGetCurrentDiscardPlayer(&session->map);
    }

    return session->map.currentPlayer;
}

static void clear_connection_error(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    session->connectionError[0] = '\0';
}

static void set_connection_error(struct MatchSession *session, const char *message)
{
    char nextMessage[sizeof(session->connectionError)];
    bool changed = false;

    if (session == NULL)
    {
        return;
    }

    snprintf(nextMessage, sizeof(nextMessage), "%s", message == NULL ? "" : message);
    changed = strncmp(session->connectionError, nextMessage, sizeof(session->connectionError)) != 0;
    snprintf(session->connectionError, sizeof(session->connectionError), "%s", nextMessage);
    if (changed && session->connectionError[0] != '\0')
    {
        debugLog("NET", "connection error (%s): %s",
                 session->networkMode == MATCH_NETWORK_PRIVATE_HOST ? "host" :
                 (session->networkMode == MATCH_NETWORK_PRIVATE_CLIENT ? "client" : "local"),
                 session->connectionError);
    }
}

static bool configure_default_private_host_seats(struct MatchSession *session)
{
    int remoteSeatCount = 0;

    if (session == NULL)
    {
        return false;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (session->seatAuthority[player] == MATCH_SEAT_REMOTE)
        {
            remoteSeatCount++;
        }
    }

    return remoteSeatCount >= 1 && remoteSeatCount <= NETPLAY_MAX_HOST_REMOTE_PLAYERS;
}

static bool is_authoritative_only_action(enum GameActionType type)
{
    return type == GAME_ACTION_ROLL_DICE ||
           type == GAME_ACTION_BUY_DEVELOPMENT ||
           type == GAME_ACTION_STEAL_RANDOM_RESOURCE;
}

static bool active_decision_is_remote(const struct MatchSession *session)
{
    return matchSessionGetSeatAuthority(session, active_decision_player(session)) == MATCH_SEAT_REMOTE;
}

static void reset_client_transient_ui(void)
{
    gBuildMode = BUILD_MODE_NONE;
    uiSetBuildPanelOpen(false);
    uiSetTradeMenuOpen(false);
    uiSetPlayerTradeMenuOpen(false);
    uiSetDevelopmentPurchaseConfirmOpen(false);
    uiSetDevelopmentPlayConfirmOpen(false);
}

void matchSessionInit(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    memset(session, 0, sizeof(*session));
    session->isHost = true;
    session->localPlayer = PLAYER_NONE;
    session->networkMode = MATCH_NETWORK_NONE;
    session->connectionStatus = MATCH_CONNECTION_LOCAL;
    session->ready = true;
    session->matchStarted = true;
    session->pendingUiResetForMatchInit = false;
    session->awaitingAuthoritativeUpdate = false;
    session->initialSnapshotReceived = false;
    clear_pending_trade_offer(session);
    session->netplay = NULL;
    clear_connection_error(session);
    session->reconnectHost[0] = '\0';
    session->reconnectPort = 0u;
    session->reconnectEnabled = false;
    session->peerCapabilityFlags = 0u;
    session->peerProtocolMinVersion = 0u;
    session->peerProtocolMaxVersion = 0u;
    session->peerCapabilitiesReceived = false;
    reset_remote_peer_assignments(session);
    reset_reconnect_state(session);
    matchSessionConfigureHotseat(session);
}

void matchSessionShutdown(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->netplay != NULL)
    {
        netplayDestroy(session->netplay);
        session->netplay = NULL;
    }

    session->networkMode = MATCH_NETWORK_NONE;
    session->connectionStatus = MATCH_CONNECTION_LOCAL;
    session->ready = true;
    session->matchStarted = false;
    session->pendingUiResetForMatchInit = false;
    session->awaitingAuthoritativeUpdate = false;
    session->initialSnapshotReceived = false;
    clear_pending_trade_offer(session);
    clear_connection_error(session);
    session->peerCapabilityFlags = 0u;
    session->peerProtocolMinVersion = 0u;
    session->peerProtocolMaxVersion = 0u;
    session->peerCapabilitiesReceived = false;
    reset_remote_peer_assignments(session);
    reset_reconnect_state(session);
}

void matchSessionConfigureHotseat(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    matchSessionShutdown(session);
    session->isHost = true;
    session->localPlayer = PLAYER_NONE;
    session->reconnectHost[0] = '\0';
    session->reconnectPort = 0u;
    session->reconnectEnabled = false;
    reset_reconnect_state(session);
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->seatAuthority[player] = MATCH_SEAT_LOCAL;
    }
    session->matchStarted = true;
}

void matchSessionConfigureSinglePlayer(struct MatchSession *session, enum PlayerType localPlayer)
{
    if (session == NULL)
    {
        return;
    }

    matchSessionShutdown(session);
    session->isHost = true;
    session->localPlayer = localPlayer;
    session->reconnectHost[0] = '\0';
    session->reconnectPort = 0u;
    session->reconnectEnabled = false;
    reset_reconnect_state(session);
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->seatAuthority[player] = player == localPlayer ? MATCH_SEAT_LOCAL : MATCH_SEAT_AI;
    }
    session->matchStarted = true;
}

void matchSessionConfigurePrivateHost(struct MatchSession *session, enum PlayerType localPlayer)
{
    if (session == NULL)
    {
        return;
    }

    matchSessionShutdown(session);
    session->isHost = true;
    session->localPlayer = localPlayer;
    session->reconnectHost[0] = '\0';
    session->reconnectPort = 0u;
    session->reconnectEnabled = false;
    reset_reconnect_state(session);
    session->networkMode = MATCH_NETWORK_PRIVATE_HOST;
    session->connectionStatus = MATCH_CONNECTION_WAITING_FOR_PLAYER;
    session->ready = false;
    session->matchStarted = false;
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->seatAuthority[player] = player == localPlayer ? MATCH_SEAT_LOCAL : MATCH_SEAT_REMOTE;
    }
}

void matchSessionConfigurePrivateClient(struct MatchSession *session, enum PlayerType localPlayer)
{
    if (session == NULL)
    {
        return;
    }

    matchSessionShutdown(session);
    session->isHost = false;
    session->localPlayer = localPlayer;
    session->reconnectHost[0] = '\0';
    session->reconnectPort = 0u;
    session->reconnectEnabled = true;
    reset_reconnect_state(session);
    session->networkMode = MATCH_NETWORK_PRIVATE_CLIENT;
    session->connectionStatus = MATCH_CONNECTION_CONNECTING;
    session->ready = false;
    session->matchStarted = false;
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->seatAuthority[player] = player == localPlayer ? MATCH_SEAT_LOCAL : MATCH_SEAT_REMOTE;
    }
}

bool matchSessionOpenPrivateHost(struct MatchSession *session, unsigned short port)
{
    if (session == NULL || session->networkMode != MATCH_NETWORK_PRIVATE_HOST || !configure_default_private_host_seats(session))
    {
        return false;
    }

    debugLog("NET", "opening private host on port %u", (unsigned int)port);
    if (session->netplay == NULL)
    {
        session->netplay = netplayCreate();
    }

    if (session->netplay == NULL || !netplayStartHost(session->netplay, port))
    {
        session->connectionStatus = MATCH_CONNECTION_ERROR;
        set_connection_error(session, session->netplay != NULL ? netplayGetLastError(session->netplay) : "socket init failed");
        return false;
    }

    session->connectionStatus = MATCH_CONNECTION_WAITING_FOR_PLAYER;
    session->ready = false;
    session->matchStarted = false;
    session->awaitingAuthoritativeUpdate = false;
    clear_pending_trade_offer(session);
    session->peerCapabilityFlags = 0u;
    session->peerProtocolMinVersion = 0u;
    session->peerProtocolMaxVersion = 0u;
    session->peerCapabilitiesReceived = false;
    clear_connection_error(session);
    return true;
}

bool matchSessionOpenPrivateClient(struct MatchSession *session, const char *hostAddress, unsigned short port)
{
    if (session == NULL || session->networkMode != MATCH_NETWORK_PRIVATE_CLIENT)
    {
        return false;
    }

    debugLog("NET", "opening private client to %s:%u", hostAddress == NULL ? "<null>" : hostAddress, (unsigned int)port);
    if (session->netplay == NULL)
    {
        session->netplay = netplayCreate();
    }

    if (session->netplay == NULL || !netplayStartClient(session->netplay, hostAddress, port))
    {
        session->connectionStatus = MATCH_CONNECTION_ERROR;
        set_connection_error(session, session->netplay != NULL ? netplayGetLastError(session->netplay) : "socket init failed");
        return false;
    }

    snprintf(session->reconnectHost, sizeof(session->reconnectHost), "%s", hostAddress);
    session->reconnectPort = port;
    session->reconnectEnabled = true;
    reset_reconnect_state(session);

    session->connectionStatus = netplayGetConnectionState(session->netplay) == NETPLAY_CONNECTION_CONNECTED
                                    ? MATCH_CONNECTION_SYNCING
                                    : MATCH_CONNECTION_CONNECTING;
    session->ready = false;
    session->matchStarted = false;
    session->awaitingAuthoritativeUpdate = false;
    clear_pending_trade_offer(session);
    session->peerCapabilityFlags = 0u;
    session->peerProtocolMinVersion = 0u;
    session->peerProtocolMaxVersion = 0u;
    session->peerCapabilitiesReceived = false;
    clear_connection_error(session);
    return true;
}

void matchSessionSetSeatAuthority(struct MatchSession *session, enum PlayerType player, enum MatchSeatAuthority authority)
{
    if (session == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return;
    }

    session->seatAuthority[player] = authority;
    if (authority != MATCH_SEAT_REMOTE)
    {
        session->remotePeerForPlayer[player] = -1;
    }
}

enum MatchSeatAuthority matchSessionGetSeatAuthority(const struct MatchSession *session, enum PlayerType player)
{
    if (session == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return MATCH_SEAT_REMOTE;
    }

    return session->seatAuthority[player];
}

enum PlayerType matchSessionGetLocalPlayer(const struct MatchSession *session)
{
    if (session == NULL)
    {
        return PLAYER_NONE;
    }

    return session->localPlayer;
}

bool matchSessionIsNetplay(const struct MatchSession *session)
{
    return session != NULL && session->networkMode != MATCH_NETWORK_NONE;
}

bool matchSessionIsReady(const struct MatchSession *session)
{
    return session == NULL || session->ready;
}

bool matchSessionHasStarted(const struct MatchSession *session)
{
    if (session == NULL)
    {
        return false;
    }

    return !matchSessionIsNetplay(session) || session->matchStarted;
}

bool matchSessionCanStartNetplayMatch(const struct MatchSession *session)
{
    return session != NULL &&
           matchSessionIsNetplay(session) &&
           session->isHost &&
           session->ready &&
           !session->matchStarted &&
           session->connectionStatus == MATCH_CONNECTION_CONNECTED &&
           session->netplay != NULL &&
           netplayIsConnected(session->netplay);
}

bool matchSessionStartNetplayMatch(struct MatchSession *session)
{
    if (!matchSessionCanStartNetplayMatch(session))
    {
        return false;
    }

    return send_match_init(session);
}

bool matchSessionRestartNetplayMatch(struct MatchSession *session)
{
    if (session == NULL ||
        !matchSessionIsNetplay(session) ||
        !session->isHost ||
        session->connectionStatus != MATCH_CONNECTION_CONNECTED ||
        session->netplay == NULL ||
        !netplayIsConnected(session->netplay))
    {
        return false;
    }

    return send_match_init(session);
}

bool matchSessionHostToggleNetplayLobbySeat(struct MatchSession *session, enum PlayerType player)
{
    enum AiDifficulty appliedDifficulty = AI_DIFFICULTY_MEDIUM;

    if (session == NULL ||
        !matchSessionIsNetplay(session) ||
        !session->isHost ||
        session->networkMode != MATCH_NETWORK_PRIVATE_HOST ||
        session->matchStarted ||
        player < PLAYER_RED ||
        player > PLAYER_BLACK ||
        session->seatAuthority[player] == MATCH_SEAT_LOCAL)
    {
        return false;
    }

    if (session->seatAuthority[player] == MATCH_SEAT_REMOTE)
    {
        if (session->remotePeerForPlayer[player] >= 0)
        {
            return false;
        }

        session->seatAuthority[player] = MATCH_SEAT_AI;
        session->remotePeerForPlayer[player] = -1;
        session->map.players[player].controlMode = PLAYER_CONTROL_AI;

        for (int i = PLAYER_RED; i <= PLAYER_BLACK; i++)
        {
            if (session->seatAuthority[i] == MATCH_SEAT_AI)
            {
                appliedDifficulty = session->map.players[i].aiDifficulty;
                break;
            }
        }
        session->map.players[player].aiDifficulty = appliedDifficulty;
        broadcast_lobby_state(session);
        return true;
    }

    if (session->seatAuthority[player] == MATCH_SEAT_AI)
    {
        session->seatAuthority[player] = MATCH_SEAT_REMOTE;
        session->remotePeerForPlayer[player] = -1;
        session->map.players[player].controlMode = PLAYER_CONTROL_HUMAN;
        session->map.players[player].aiDifficulty = AI_DIFFICULTY_EASY;
        broadcast_lobby_state(session);
        return true;
    }

    return false;
}

bool matchSessionHostCycleNetplayLobbyAiDifficulty(struct MatchSession *session)
{
    enum AiDifficulty nextDifficulty = AI_DIFFICULTY_MEDIUM;
    bool foundAiSeat = false;

    if (session == NULL ||
        !matchSessionIsNetplay(session) ||
        !session->isHost ||
        session->networkMode != MATCH_NETWORK_PRIVATE_HOST ||
        session->matchStarted)
    {
        return false;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (session->seatAuthority[player] == MATCH_SEAT_AI)
        {
            nextDifficulty = (enum AiDifficulty)(((int)session->map.players[player].aiDifficulty + 1) % 3);
            foundAiSeat = true;
            break;
        }
    }

    if (!foundAiSeat)
    {
        return false;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (session->seatAuthority[player] == MATCH_SEAT_AI)
        {
            session->map.players[player].aiDifficulty = nextDifficulty;
        }
    }

    broadcast_lobby_state(session);
    return true;
}

bool matchSessionShouldAnimateLocalRoll(const struct MatchSession *session)
{
    return session == NULL || !matchSessionIsNetplay(session) || session->isHost;
}

bool matchSessionConsumePendingMatchInitUiReset(struct MatchSession *session)
{
    if (session == NULL || !session->pendingUiResetForMatchInit)
    {
        return false;
    }

    session->pendingUiResetForMatchInit = false;
    return true;
}

bool matchSessionShouldRunAi(const struct MatchSession *session)
{
    if (session == NULL)
    {
        return true;
    }

    return session->isHost && session->ready && matchSessionHasStarted(session);
}

bool matchSessionIsHost(const struct MatchSession *session)
{
    return session != NULL && session->isHost;
}

enum MatchConnectionStatus matchSessionGetConnectionStatus(const struct MatchSession *session)
{
    return session == NULL ? MATCH_CONNECTION_LOCAL : session->connectionStatus;
}

const char *matchSessionGetConnectionError(const struct MatchSession *session)
{
    return session == NULL ? "" : session->connectionError;
}

bool matchSessionLocalControlsPlayer(const struct MatchSession *session, enum PlayerType player)
{
    if (session == NULL ||
        player < PLAYER_RED ||
        player > PLAYER_BLACK ||
        !session->ready ||
        !matchSessionHasStarted(session) ||
        session->awaitingAuthoritativeUpdate)
    {
        return false;
    }

    if (session->map.players[player].controlMode == PLAYER_CONTROL_AI)
    {
        return false;
    }

    return session->seatAuthority[player] == MATCH_SEAT_LOCAL;
}

bool matchSessionLocalCanActOnCurrentDecision(const struct MatchSession *session)
{
    return matchSessionLocalControlsPlayer(session, active_decision_player(session));
}

static bool apply_authoritative_action_to_client_map(struct MatchSession *session,
                                                     const struct GameAction *action,
                                                     const struct GameActionResult *result)
{
    if (session == NULL || action == NULL || result == NULL)
    {
        return false;
    }

    if (action->type == GAME_ACTION_STEAL_RANDOM_RESOURCE)
    {
        if (!gameCanStealFromPlayer(&session->map, action->player) ||
            result->stolenResource < RESOURCE_WOOD ||
            result->stolenResource > RESOURCE_STONE ||
            session->map.players[action->player].resources[result->stolenResource] <= 0)
        {
            return false;
        }

        session->map.players[action->player].resources[result->stolenResource]--;
        session->map.players[session->map.currentPlayer].resources[result->stolenResource]++;
        session->map.awaitingThiefVictimSelection = false;
        return true;
    }

    return gameApplyAction(&session->map, action, NULL, NULL);
}

static void apply_client_authoritative_result(struct MatchSession *session,
                                              const struct GameAction *action,
                                              const struct GameActionResult *result,
                                              uint32_t authoritativeStateHash)
{
    bool needsApply = false;
    const enum PlayerType actor = active_decision_player(session);

    if (session == NULL || action == NULL || result == NULL)
    {
        return;
    }

    needsApply = !action_actor_is_local_viewer(session, actor) || is_authoritative_only_action(action->type);
    if (needsApply)
    {
        if (!apply_authoritative_action_to_client_map(session, action, result))
        {
            debugLog("NET", "client apply failed action=%d hash=%u", (int)action->type, authoritativeStateHash);
            request_snapshot_resync(session, "client apply failed");
            return;
        }
        matchSessionRefreshStateHash(session);
        if (authoritativeStateHash != 0u && session->stateHash != authoritativeStateHash)
        {
            debugLog("NET", "client hash mismatch local=%u authoritative=%u action=%d",
                     session->stateHash,
                     authoritativeStateHash,
                     (int)action->type);
            request_snapshot_resync(session, "action result hash mismatch");
        }
    }
    else if (authoritativeStateHash != 0u && session->stateHash != authoritativeStateHash)
    {
        debugLog("NET", "client predicted hash mismatch local=%u authoritative=%u action=%d",
                 session->stateHash,
                 authoritativeStateHash,
                 (int)action->type);
        request_snapshot_resync(session, "predicted hash mismatch");
    }

    if (needsApply)
    {
        show_action_feedback(session, actor, action, result);
    }

    if (action->type == GAME_ACTION_ROLL_DICE && result->diceRoll >= 2 && result->diceRoll <= 12)
    {
        uiStartObservedDiceRollAnimation(result->diceRoll);
    }

    if (action->type == GAME_ACTION_BUY_DEVELOPMENT &&
        result->drawnCard >= DEVELOPMENT_CARD_KNIGHT &&
        result->drawnCard < DEVELOPMENT_CARD_COUNT)
    {
        uiStartDevelopmentCardDrawAnimation(result->drawnCard);
    }
}

static bool submit_client_action(struct MatchSession *session,
                                 const struct GameAction *action,
                                 const struct GameActionContext *context,
                                 struct GameActionResult *result)
{
    struct GameAction outboundAction;
    struct GameActionResult localResult;
    struct GameActionResult *applyResult = result;
    const enum PlayerType actor = active_decision_player(session);
    bool appliedLocally = false;
    const bool remoteHumanTrade = action != NULL &&
                                  action->type == GAME_ACTION_TRADE_WITH_PLAYER &&
                                  matchSessionGetSeatAuthority(session, action->player) == MATCH_SEAT_REMOTE;

    if (session == NULL || session->netplay == NULL || action == NULL || session->awaitingAuthoritativeUpdate)
    {
        return false;
    }

    outboundAction = *action;
    if (outboundAction.type == GAME_ACTION_ROLL_DICE)
    {
        outboundAction.diceRoll = 0;
    }

    if (!is_authoritative_only_action(action->type) && !remoteHumanTrade)
    {
        struct Map simulatedMap = session->map;
        struct GameActionResult simulatedResult;

        init_action_result(&simulatedResult);
        if (!gameApplyAction(&simulatedMap, action, context, &simulatedResult))
        {
            return false;
        }
    }

    if (!netplayQueueActionRequest(session->netplay, &outboundAction))
    {
        return false;
    }

    if (!is_authoritative_only_action(action->type) && !remoteHumanTrade)
    {
        if (applyResult == NULL)
        {
            init_action_result(&localResult);
            applyResult = &localResult;
        }

        appliedLocally = gameApplyAction(&session->map, action, context, applyResult);
        if (!appliedLocally)
        {
            return false;
        }
        matchSessionRefreshStateHash(session);
        show_action_feedback(session, actor, action, applyResult);
    }
    else if (result != NULL)
    {
        init_action_result(result);
    }

    if (remoteHumanTrade)
    {
        session->pendingTradeOfferActive = true;
        session->pendingTradeAwaitingLocalResponse = false;
        session->pendingTradeRequestedByRemote = false;
        session->pendingTradeOffer = *action;
        uiShowCenteredStatus(loc("Trade offer sent to host."), UI_NOTIFICATION_NEUTRAL);
    }

    session->awaitingAuthoritativeUpdate = true;
    return true;
}

static bool broadcast_host_snapshot(struct MatchSession *session)
{
    unsigned char snapshotBuffer[NETPLAY_MAX_PAYLOAD_SIZE];
    const size_t snapshotSize = mapSerializeSnapshot(&session->map, snapshotBuffer, sizeof(snapshotBuffer));

    if (session == NULL || session->netplay == NULL || !netplayIsConnected(session->netplay))
    {
        return false;
    }

    if (snapshotSize == 0u)
    {
        return false;
    }

    return netplayQueueSnapshot(session->netplay, snapshotBuffer, snapshotSize);
}

bool matchSessionBroadcastState(struct MatchSession *session)
{
    return broadcast_host_snapshot(session);
}

bool matchSessionHasPendingTradeOfferForLocalResponse(const struct MatchSession *session)
{
    return session != NULL &&
           session->pendingTradeOfferActive &&
           session->pendingTradeAwaitingLocalResponse;
}

bool matchSessionGetPendingTradeOffer(const struct MatchSession *session, struct GameAction *offerAction)
{
    if (session == NULL || offerAction == NULL || !session->pendingTradeOfferActive)
    {
        return false;
    }

    *offerAction = session->pendingTradeOffer;
    return true;
}

bool matchSessionRespondToPendingTradeOffer(struct MatchSession *session, bool accept)
{
    bool applied = false;
    struct GameActionResult actionResult;

    if (!matchSessionHasPendingTradeOfferForLocalResponse(session) || session->netplay == NULL)
    {
        return false;
    }

    init_action_result(&actionResult);
    if (accept)
    {
        applied = gameApplyAction(&session->map, &session->pendingTradeOffer, NULL, &actionResult);
        if (applied)
        {
            matchSessionRefreshStateHash(session);
        }
    }

    if (session->pendingTradeRequestedByRemote)
    {
        if (session->pendingTradePeerId >= 0)
        {
            netplayQueueTradeResponseForHostPeer(session->netplay,
                                                 session->pendingTradePeerId,
                                                 &session->pendingTradeOffer,
                                                 applied,
                                                 session->stateHash);
        }
        else
        {
            netplayQueueTradeResponse(session->netplay, &session->pendingTradeOffer, applied, session->stateHash);
        }
    }

    clear_pending_trade_offer(session);
    return true;
}

bool matchSessionSubmitAction(struct MatchSession *session,
                              const struct GameAction *action,
                              const struct GameActionContext *context,
                              struct GameActionResult *result)
{
    bool applied = false;
    struct GameActionResult localResult;
    struct GameActionResult authoritativeResult;
    struct GameActionResult *authoritativeResultPtr = result;
    const enum PlayerType actor = active_decision_player(session);

    if (session == NULL || action == NULL)
    {
        return false;
    }

    if (!matchSessionIsNetplay(session))
    {
        if (authoritativeResultPtr == NULL)
        {
            init_action_result(&localResult);
            authoritativeResultPtr = &localResult;
        }

        applied = gameApplyAction(&session->map, action, context, authoritativeResultPtr);
        if (applied)
        {
            matchSessionRefreshStateHash(session);
            show_action_feedback(session, actor, action, authoritativeResultPtr);
        }
        return applied;
    }

    if (!session->ready || !matchSessionHasStarted(session))
    {
        return false;
    }

    if (!session->isHost)
    {
        return submit_client_action(session, action, context, result);
    }

    if (action->type == GAME_ACTION_TRADE_WITH_PLAYER &&
        matchSessionGetSeatAuthority(session, action->player) == MATCH_SEAT_REMOTE)
    {
        const int targetPeerId = (action->player >= PLAYER_RED && action->player <= PLAYER_BLACK)
                                     ? session->remotePeerForPlayer[action->player]
                                     : -1;
        if (session->pendingTradeOfferActive || session->netplay == NULL || !netplayIsConnected(session->netplay))
        {
            return false;
        }

        if (targetPeerId < 0)
        {
            return false;
        }

        if (!netplayQueueTradeOfferForHostPeer(session->netplay, targetPeerId, action))
        {
            return false;
        }

        session->pendingTradeOfferActive = true;
        session->pendingTradeAwaitingLocalResponse = false;
        session->pendingTradeRequestedByRemote = false;
        session->pendingTradePeerId = targetPeerId;
        session->pendingTradeOffer = *action;
        session->awaitingAuthoritativeUpdate = true;
        uiShowCenteredStatus(loc("Trade offer sent to remote player."), UI_NOTIFICATION_NEUTRAL);
        if (result != NULL)
        {
            init_action_result(result);
        }
        return true;
    }

    if (authoritativeResultPtr == NULL)
    {
        init_action_result(&authoritativeResult);
        authoritativeResultPtr = &authoritativeResult;
    }

    applied = gameApplyAction(&session->map, action, context, authoritativeResultPtr);
    if (!applied)
    {
        return false;
    }

    matchSessionRefreshStateHash(session);
    show_action_feedback(session, actor, action, authoritativeResultPtr);
    if (session->netplay != NULL && netplayIsConnected(session->netplay))
    {
        netplayQueueActionResult(session->netplay, action, authoritativeResultPtr, session->stateHash);
    }
    return true;
}

static void handle_netplay_event(struct MatchSession *session, const struct NetplayEvent *event)
{
    if (session == NULL || event == NULL)
    {
        return;
    }

    switch (event->type)
    {
    case NETPLAY_EVENT_CLIENT_CONNECTED:
        {
            const int peerId = event->peerId >= 0 ? event->peerId : 0;
            const enum PlayerType remotePlayer = assign_remote_player_to_peer(session, peerId);

            session->connectionStatus = MATCH_CONNECTION_CONNECTED;
            session->ready = true;
            session->matchStarted = false;
            clear_connection_error(session);
            debugLog("NET",
                     "remote client connected (peer=%d local=%d remote=%d)",
                     peerId,
                     (int)session->localPlayer,
                     (int)remotePlayer);
            uiShowCenteredStatus(loc("Remote player connected."), UI_NOTIFICATION_POSITIVE);
            if (session->netplay != NULL)
            {
                send_hello_to_peer(session, peerId, remotePlayer);
                queue_local_capabilities(session);
                broadcast_lobby_state(session);
            }
        }
        break;

    case NETPLAY_EVENT_ADDITIONAL_CLIENT_CONNECTED:
        if (session->isHost)
        {
            const enum PlayerType remotePlayer = assign_remote_player_to_peer(session, event->peerId);

            debugLog("NET",
                     "additional remote connected (peer=%d remote=%d)",
                     event->peerId,
                     (int)remotePlayer);
            if (remotePlayer == PLAYER_NONE)
            {
                uiShowCenteredWarning(loc("Remote connection accepted, but no free remote seat is available."));
                break;
            }

            uiShowCenteredStatus(loc("Remote player connected."), UI_NOTIFICATION_POSITIVE);
            send_hello_to_peer(session, event->peerId, remotePlayer);
            queue_local_capabilities(session);
            broadcast_lobby_state(session);
        }
        break;

    case NETPLAY_EVENT_CONNECTED:
        session->connectionStatus = MATCH_CONNECTION_SYNCING;
        reset_reconnect_state(session);
        clear_connection_error(session);
        debugLog("NET", "client connected to host; waiting for hello/lobby");
        uiShowCenteredStatus(loc("Connected to host."), UI_NOTIFICATION_NEUTRAL);
        queue_local_capabilities(session);
        break;

    case NETPLAY_EVENT_DISCONNECTED:
        session->ready = false;
        session->matchStarted = false;
        session->pendingUiResetForMatchInit = false;
        session->awaitingAuthoritativeUpdate = false;
        clear_pending_trade_offer(session);
        session->peerCapabilityFlags = 0u;
        session->peerProtocolMinVersion = 0u;
        session->peerProtocolMaxVersion = 0u;
        session->peerCapabilitiesReceived = false;
        reset_remote_peer_assignments(session);
        session->connectionStatus = session->networkMode == MATCH_NETWORK_PRIVATE_HOST
                                        ? MATCH_CONNECTION_WAITING_FOR_PLAYER
                                        : MATCH_CONNECTION_DISCONNECTED;
        set_connection_error(session, event->message);
        reset_client_transient_ui();
        debugLog("NET", "disconnected event: %s", event->message);
        uiShowCenteredWarning(loc("Remote player disconnected."));
        if (session->networkMode == MATCH_NETWORK_PRIVATE_HOST)
        {
            for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
            {
                if (session->seatAuthority[player] == MATCH_SEAT_REMOTE)
                {
                    session->seatAuthority[player] = MATCH_SEAT_AI;
                    session->map.players[player].controlMode = PLAYER_CONTROL_AI;
                }
            }
            broadcast_lobby_state(session);
        }
        if (session->networkMode == MATCH_NETWORK_PRIVATE_CLIENT)
        {
            schedule_client_reconnect(session, CLIENT_RECONNECT_DELAY_SECONDS);
        }
        break;

    case NETPLAY_EVENT_ADDITIONAL_CLIENT_DISCONNECTED:
        if (session->isHost)
        {
            clear_remote_peer_assignment_for_peer(session, event->peerId);
            broadcast_lobby_state(session);
            debugLog("NET", "additional remote disconnected (peer=%d: %s)", event->peerId, event->message);
            uiShowCenteredWarning(loc("Remote player disconnected."));
        }
        break;

    case NETPLAY_EVENT_HELLO:
        {
            enum PlayerType inferredLocalPlayer = PLAYER_NONE;

        session->isHost = false;
        session->localPlayer = event->hello.assignedPlayer != PLAYER_NONE ? event->hello.assignedPlayer : session->localPlayer;
        if (session->localPlayer == PLAYER_NONE)
        {
            for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
            {
                if (event->hello.seatAuthority[player] == MATCH_SEAT_REMOTE)
                {
                    inferredLocalPlayer = (enum PlayerType)player;
                    break;
                }
            }
        }
        if (session->localPlayer == PLAYER_NONE && inferredLocalPlayer != PLAYER_NONE)
        {
            session->localPlayer = inferredLocalPlayer;
        }

        for (int player = 0; player < MAX_PLAYERS; player++)
        {
            const enum MatchSeatAuthority hostPerspectiveAuthority = (enum MatchSeatAuthority)event->hello.seatAuthority[player];
            if (hostPerspectiveAuthority == MATCH_SEAT_AI)
            {
                session->seatAuthority[player] = MATCH_SEAT_AI;
            }
            else if (session->localPlayer != PLAYER_NONE && player == (int)session->localPlayer)
            {
                session->seatAuthority[player] = MATCH_SEAT_LOCAL;
            }
            else
            {
                session->seatAuthority[player] = MATCH_SEAT_REMOTE;
            }
        }
        session->connectionStatus = MATCH_CONNECTION_SYNCING;
        reset_reconnect_state(session);
        session->ready = false;
        session->matchStarted = false;
        session->awaitingAuthoritativeUpdate = false;
        clear_connection_error(session);
        reset_client_transient_ui();
        debugLog("NET",
             "hello received; assigned local=%d host=%d client-authorities=[%d,%d,%d,%d]",
             (int)session->localPlayer,
             (int)event->hello.hostPlayer,
             (int)session->seatAuthority[0],
             (int)session->seatAuthority[1],
             (int)session->seatAuthority[2],
             (int)session->seatAuthority[3]);
        if (session->localPlayer == PLAYER_NONE)
        {
            debugLog("NET", "warning: host hello assigned PLAYER_NONE; awaiting lobby state but local actions will stay blocked");
        }
        }
        break;

    case NETPLAY_EVENT_LOBBY_STATE:
        apply_lobby_state_info(session, &event->lobbyState);
        session->ready = true;
        session->matchStarted = false;
        session->pendingUiResetForMatchInit = false;
        session->awaitingAuthoritativeUpdate = false;
        session->connectionStatus = MATCH_CONNECTION_CONNECTED;
        reset_reconnect_state(session);
        clear_connection_error(session);
        break;

    case NETPLAY_EVENT_MATCH_INIT:
        if (apply_match_init_info(session, &event->matchInit))
        {
            session->ready = true;
            session->matchStarted = true;
            session->pendingUiResetForMatchInit = true;
            session->awaitingAuthoritativeUpdate = false;
            session->initialSnapshotReceived = false;
            session->connectionStatus = MATCH_CONNECTION_CONNECTED;
            reset_reconnect_state(session);
            clear_connection_error(session);
            reset_client_transient_ui();
            debugLog("NET", "match init applied hash=%u", session->stateHash);
        }
        else
        {
            debugLog("NET", "match init rejected");
        }
        break;

    case NETPLAY_EVENT_SNAPSHOT:
        if (mapDeserializeSnapshot(&session->map, event->payload, event->payloadSize))
        {
            session->ready = true;
            session->awaitingAuthoritativeUpdate = false;
            session->initialSnapshotReceived = true;
            session->connectionStatus = MATCH_CONNECTION_CONNECTED;
            reset_reconnect_state(session);
            clear_connection_error(session);
            matchSessionRefreshStateHash(session);
            reset_client_transient_ui();
            debugLog("NET", "snapshot applied (size=%zu, hash=%u)", event->payloadSize, session->stateHash);
        }
        else
        {
            debugLog("NET", "snapshot rejected (size=%zu, expected=%zu)",
                     event->payloadSize,
                     mapSnapshotSerializedSize());
        }
        break;

    case NETPLAY_EVENT_ACTION_REQUEST:
        if (session->isHost && active_decision_is_remote(session))
        {
            struct GameAction authoritativeAction = event->action;
            struct GameActionResult actionResult;
            const enum PlayerType actor = active_decision_player(session);
            const int expectedPeerId = (actor >= PLAYER_RED && actor <= PLAYER_BLACK)
                                           ? session->remotePeerForPlayer[actor]
                                           : -1;

            if (expectedPeerId >= 0 && event->peerId >= 0 && event->peerId != expectedPeerId)
            {
                if (session->netplay != NULL)
                {
                    netplayQueueActionReject(session->netplay, "action submitted by wrong remote peer", session->stateHash);
                }
                break;
            }

            if (authoritativeAction.type == GAME_ACTION_ROLL_DICE)
            {
                authoritativeAction.diceRoll = GetRandomValue(1, 6) + GetRandomValue(1, 6);
            }

            if (authoritativeAction.type == GAME_ACTION_TRADE_WITH_PLAYER &&
                matchSessionGetSeatAuthority(session, authoritativeAction.player) != MATCH_SEAT_AI)
            {
                if (matchSessionGetSeatAuthority(session, authoritativeAction.player) == MATCH_SEAT_LOCAL)
                {
                    if (!session->pendingTradeOfferActive)
                    {
                        session->pendingTradeOfferActive = true;
                        session->pendingTradeAwaitingLocalResponse = true;
                        session->pendingTradeRequestedByRemote = true;
                        session->pendingTradePeerId = event->peerId;
                        session->pendingTradeOffer = authoritativeAction;
                        uiShowCenteredStatus(loc("Remote trade offer received."), UI_NOTIFICATION_NEUTRAL);
                    }
                    break;
                }

                netplayQueueActionReject(session->netplay, "trade target unsupported", session->stateHash);
                break;
            }

            init_action_result(&actionResult);
            if (gameApplyAction(&session->map, &authoritativeAction, NULL, &actionResult))
            {
                matchSessionRefreshStateHash(session);
                show_action_feedback(session, actor, &authoritativeAction, &actionResult);
                if (session->netplay != NULL)
                {
                    netplayQueueActionResult(session->netplay, &authoritativeAction, &actionResult, session->stateHash);
                }
            }
            else if (session->netplay != NULL)
            {
                netplayQueueActionReject(session->netplay, "action rejected", session->stateHash);
                broadcast_host_snapshot(session);
            }
        }
        else if (session->isHost && session->netplay != NULL)
        {
            netplayQueueActionReject(session->netplay, "not remote turn", session->stateHash);
            broadcast_host_snapshot(session);
        }
        break;

    case NETPLAY_EVENT_ACTION_RESULT:
        session->awaitingAuthoritativeUpdate = false;
        apply_client_authoritative_result(session, &event->action, &event->result, event->stateHash);
        break;

    case NETPLAY_EVENT_ACTION_REJECT:
        session->awaitingAuthoritativeUpdate = false;
        if (event->message[0] != '\0')
        {
            char warning[160];
            snprintf(warning, sizeof(warning), loc("Action rejected by host: %s"), event->message);
            uiShowCenteredWarning(warning);
        }
        else
        {
            uiShowCenteredWarning(loc("Action rejected by host."));
        }
        if (event->stateHash != 0u && event->stateHash != session->stateHash)
        {
            debugLog("NET", "action reject hash mismatch local=%u host=%u", session->stateHash, event->stateHash);
            uiShowCenteredWarning(loc("State mismatch detected; waiting for resync snapshot."));
            request_snapshot_resync(session, "action reject hash mismatch");
        }
        break;

    case NETPLAY_EVENT_TRADE_OFFER:
        if (!session->isHost &&
            event->action.type == GAME_ACTION_TRADE_WITH_PLAYER &&
            matchSessionGetSeatAuthority(session, event->action.player) == MATCH_SEAT_LOCAL)
        {
            session->pendingTradeOfferActive = true;
            session->pendingTradeAwaitingLocalResponse = true;
            session->pendingTradeRequestedByRemote = true;
            session->pendingTradeOffer = event->action;
            uiShowCenteredStatus(loc("Trade offer received."), UI_NOTIFICATION_NEUTRAL);
        }
        break;

    case NETPLAY_EVENT_TRADE_RESPONSE:
        if (session->isHost)
        {
            if (session->pendingTradeOfferActive &&
                !session->pendingTradeAwaitingLocalResponse &&
                !session->pendingTradeRequestedByRemote &&
                (session->pendingTradePeerId < 0 || session->pendingTradePeerId == event->peerId) &&
                actions_match(&session->pendingTradeOffer, &event->action))
            {
                if (event->accepted)
                {
                    struct GameActionResult actionResult;
                    init_action_result(&actionResult);
                    if (gameApplyAction(&session->map, &session->pendingTradeOffer, NULL, &actionResult))
                    {
                        matchSessionRefreshStateHash(session);
                        uiShowCenteredStatus(loc("Remote player accepted your trade."), UI_NOTIFICATION_POSITIVE);
                    }
                    else
                    {
                        uiShowCenteredWarning(loc("Trade failed on host."));
                    }
                }
                else
                {
                    uiShowCenteredStatus(loc("Remote player declined your trade."), UI_NOTIFICATION_NEGATIVE);
                }

                session->awaitingAuthoritativeUpdate = false;
                clear_pending_trade_offer(session);
            }
        }
        else
        {
            if (session->pendingTradeOfferActive && actions_match(&session->pendingTradeOffer, &event->action))
            {
                session->awaitingAuthoritativeUpdate = false;
                if (event->accepted)
                {
                    if (gameApplyAction(&session->map, &event->action, NULL, NULL))
                    {
                        matchSessionRefreshStateHash(session);
                        if (event->stateHash != 0u && event->stateHash != session->stateHash)
                        {
                            debugLog("NET", "trade response hash mismatch local=%u host=%u", session->stateHash, event->stateHash);
                            uiShowCenteredWarning(loc("Trade applied with state mismatch; waiting for resync snapshot."));
                            request_snapshot_resync(session, "trade response hash mismatch");
                        }
                    }
                    uiShowCenteredStatus(loc("Host accepted your trade."), UI_NOTIFICATION_POSITIVE);
                }
                else
                {
                    uiShowCenteredStatus(loc("Host declined your trade."), UI_NOTIFICATION_NEGATIVE);
                }
                clear_pending_trade_offer(session);
            }
        }
        break;

    case NETPLAY_EVENT_CAPABILITIES:
        session->peerCapabilityFlags = event->capabilityFlags;
        session->peerProtocolMinVersion = event->protocolMinVersion;
        session->peerProtocolMaxVersion = event->protocolMaxVersion;
        session->peerCapabilitiesReceived = true;

        if (event->protocolMinVersion > event->protocolMaxVersion ||
            MATCH_PROTOCOL_MIN_VERSION > event->protocolMaxVersion ||
            MATCH_PROTOCOL_MAX_VERSION < event->protocolMinVersion)
        {
            debugLog("NET",
                     "incompatible protocol range peer=[%u,%u] local=[%u,%u]",
                     event->protocolMinVersion,
                     event->protocolMaxVersion,
                     (unsigned int)MATCH_PROTOCOL_MIN_VERSION,
                     (unsigned int)MATCH_PROTOCOL_MAX_VERSION);
            set_connection_error(session, "incompatible protocol version");
            session->connectionStatus = MATCH_CONNECTION_ERROR;
            uiShowCenteredWarning(loc("Version mismatch with peer."));
        }
        break;

    case NETPLAY_EVENT_RESYNC_REQUEST:
        if (session->isHost)
        {
            debugLog("NET", "peer requested resync hash=%u reason=%s", event->stateHash, event->message);
            broadcast_host_snapshot(session);
        }
        break;

    case NETPLAY_EVENT_NONE:
    default:
        break;
    }
}

void matchSessionUpdate(struct MatchSession *session)
{
    struct NetplayEvent event;

    if (session == NULL || session->netplay == NULL)
    {
        return;
    }

    netplayUpdate(session->netplay);
    if (netplayGetConnectionState(session->netplay) == NETPLAY_CONNECTION_ERROR)
    {
        session->connectionStatus = session->networkMode == MATCH_NETWORK_PRIVATE_CLIENT
                                        ? MATCH_CONNECTION_DISCONNECTED
                                        : MATCH_CONNECTION_ERROR;
        set_connection_error(session, netplayGetLastError(session->netplay));
        if (session->networkMode == MATCH_NETWORK_PRIVATE_CLIENT)
        {
            schedule_client_reconnect(session, CLIENT_RECONNECT_DELAY_SECONDS);
        }
    }

    while (netplayPollEvent(session->netplay, &event))
    {
        handle_netplay_event(session, &event);
    }

    attempt_client_reconnect(session);
}

void matchSessionRefreshStateHash(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    session->stateHash = mapComputeSnapshotHash(&session->map);
}

uint32_t matchSessionGetStateHash(const struct MatchSession *session)
{
    if (session == NULL)
    {
        return 0u;
    }

    return session->stateHash;
}

void matchSessionSetActive(struct MatchSession *session)
{
    gActiveSession = session;
}

const struct MatchSession *matchSessionGetActive(void)
{
    return gActiveSession;
}

struct MatchSession *matchSessionGetActiveMutable(void)
{
    return gActiveSession;
}
