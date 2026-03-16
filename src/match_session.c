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
static bool submit_client_action(struct MatchSession *session,
                                 const struct GameAction *action,
                                 const struct GameActionContext *context,
                                 struct GameActionResult *result);
static void apply_client_authoritative_result(const struct GameAction *action, const struct GameActionResult *result);
static bool broadcast_host_snapshot(struct MatchSession *session);
static void handle_netplay_event(struct MatchSession *session, const struct NetplayEvent *event);
static void reset_client_transient_ui(void);
static void clear_pending_trade_offer(struct MatchSession *session);
static bool actions_match(const struct GameAction *a, const struct GameAction *b);

static void clear_pending_trade_offer(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    session->pendingTradeOfferActive = false;
    session->pendingTradeAwaitingLocalResponse = false;
    session->pendingTradeRequestedByRemote = false;
    memset(&session->pendingTradeOffer, 0, sizeof(session->pendingTradeOffer));
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

    return remoteSeatCount == 1;
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
    session->awaitingAuthoritativeUpdate = false;
    session->initialSnapshotReceived = false;
    clear_pending_trade_offer(session);
    session->netplay = NULL;
    clear_connection_error(session);
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
    session->awaitingAuthoritativeUpdate = false;
    session->initialSnapshotReceived = false;
    clear_pending_trade_offer(session);
    clear_connection_error(session);
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
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->seatAuthority[player] = MATCH_SEAT_LOCAL;
    }
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
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        session->seatAuthority[player] = player == localPlayer ? MATCH_SEAT_LOCAL : MATCH_SEAT_AI;
    }
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
    session->networkMode = MATCH_NETWORK_PRIVATE_HOST;
    session->connectionStatus = MATCH_CONNECTION_WAITING_FOR_PLAYER;
    session->ready = false;
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
    session->networkMode = MATCH_NETWORK_PRIVATE_CLIENT;
    session->connectionStatus = MATCH_CONNECTION_CONNECTING;
    session->ready = false;
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
    session->awaitingAuthoritativeUpdate = false;
    clear_pending_trade_offer(session);
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

    session->connectionStatus = netplayGetConnectionState(session->netplay) == NETPLAY_CONNECTION_CONNECTED
                                    ? MATCH_CONNECTION_SYNCING
                                    : MATCH_CONNECTION_CONNECTING;
    session->ready = false;
    session->awaitingAuthoritativeUpdate = false;
    clear_pending_trade_offer(session);
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

bool matchSessionShouldAnimateLocalRoll(const struct MatchSession *session)
{
    return session == NULL || !matchSessionIsNetplay(session) || session->isHost;
}

bool matchSessionShouldRunAi(const struct MatchSession *session)
{
    if (session == NULL)
    {
        return true;
    }

    return session->isHost && session->ready;
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
    if (session == NULL || player < PLAYER_RED || player > PLAYER_BLACK || !session->ready || session->awaitingAuthoritativeUpdate)
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

static void apply_client_authoritative_result(const struct GameAction *action, const struct GameActionResult *result)
{
    if (action == NULL || result == NULL)
    {
        return;
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

        memset(&simulatedResult, 0, sizeof(simulatedResult));
        simulatedResult.drawnCard = DEVELOPMENT_CARD_COUNT;
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
        appliedLocally = gameApplyAction(&session->map, action, context, result);
        if (!appliedLocally)
        {
            return false;
        }
        matchSessionRefreshStateHash(session);
    }
    else if (result != NULL)
    {
        memset(result, 0, sizeof(*result));
        result->drawnCard = DEVELOPMENT_CARD_COUNT;
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

    memset(&actionResult, 0, sizeof(actionResult));
    actionResult.drawnCard = DEVELOPMENT_CARD_COUNT;
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
        netplayQueueTradeResponse(session->netplay, &session->pendingTradeOffer, applied, session->stateHash);
        if (applied)
        {
            broadcast_host_snapshot(session);
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

    if (session == NULL || action == NULL)
    {
        return false;
    }

    if (!matchSessionIsNetplay(session))
    {
        applied = gameApplyAction(&session->map, action, context, result);
        if (applied)
        {
            matchSessionRefreshStateHash(session);
        }
        return applied;
    }

    if (!session->ready)
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
        if (session->pendingTradeOfferActive || session->netplay == NULL || !netplayIsConnected(session->netplay))
        {
            return false;
        }

        if (!netplayQueueTradeOffer(session->netplay, action))
        {
            return false;
        }

        session->pendingTradeOfferActive = true;
        session->pendingTradeAwaitingLocalResponse = false;
        session->pendingTradeRequestedByRemote = false;
        session->pendingTradeOffer = *action;
        session->awaitingAuthoritativeUpdate = true;
        uiShowCenteredStatus(loc("Trade offer sent to remote player."), UI_NOTIFICATION_NEUTRAL);
        if (result != NULL)
        {
            memset(result, 0, sizeof(*result));
            result->drawnCard = DEVELOPMENT_CARD_COUNT;
        }
        return true;
    }

    applied = gameApplyAction(&session->map, action, context, result);
    if (!applied)
    {
        return false;
    }

    matchSessionRefreshStateHash(session);
    if (session->netplay != NULL && netplayIsConnected(session->netplay))
    {
        if (result != NULL)
        {
            netplayQueueActionResult(session->netplay, action, result, session->stateHash);
        }
        broadcast_host_snapshot(session);
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
            enum PlayerType remotePlayer = PLAYER_NONE;
            int authorities[MAX_PLAYERS] = {0};

            for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
            {
                authorities[player] = session->seatAuthority[player];
                if (session->seatAuthority[player] == MATCH_SEAT_REMOTE)
                {
                    remotePlayer = (enum PlayerType)player;
                }
            }

            session->connectionStatus = MATCH_CONNECTION_CONNECTED;
            session->ready = true;
            clear_connection_error(session);
            debugLog("NET", "remote client connected (local=%d remote=%d)", (int)session->localPlayer, (int)remotePlayer);
            uiShowCenteredStatus(loc("Remote player connected."), UI_NOTIFICATION_POSITIVE);
            if (session->netplay != NULL)
            {
                netplayQueueHello(session->netplay, remotePlayer, session->localPlayer, authorities);
                broadcast_host_snapshot(session);
            }
        }
        break;

    case NETPLAY_EVENT_CONNECTED:
        session->connectionStatus = MATCH_CONNECTION_SYNCING;
        clear_connection_error(session);
        debugLog("NET", "client connected to host; waiting for hello/snapshot");
        uiShowCenteredStatus(loc("Connected to host."), UI_NOTIFICATION_NEUTRAL);
        break;

    case NETPLAY_EVENT_DISCONNECTED:
        session->ready = false;
        session->awaitingAuthoritativeUpdate = false;
        clear_pending_trade_offer(session);
        session->connectionStatus = session->networkMode == MATCH_NETWORK_PRIVATE_HOST
                                        ? MATCH_CONNECTION_WAITING_FOR_PLAYER
                                        : MATCH_CONNECTION_DISCONNECTED;
        set_connection_error(session, event->message);
        reset_client_transient_ui();
        debugLog("NET", "disconnected event: %s", event->message);
        uiShowCenteredWarning(loc("Remote player disconnected."));
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
        session->ready = false;
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
            debugLog("NET", "warning: host hello assigned PLAYER_NONE; awaiting snapshot but local actions will stay blocked");
        }
        }
        break;

    case NETPLAY_EVENT_SNAPSHOT:
        if (mapDeserializeSnapshot(&session->map, event->payload, event->payloadSize))
        {
            session->ready = true;
            session->awaitingAuthoritativeUpdate = false;
            session->initialSnapshotReceived = true;
            session->connectionStatus = MATCH_CONNECTION_CONNECTED;
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
                        session->pendingTradeOffer = authoritativeAction;
                        uiShowCenteredStatus(loc("Remote trade offer received."), UI_NOTIFICATION_NEUTRAL);
                    }
                    break;
                }

                netplayQueueActionReject(session->netplay, "trade target unsupported", session->stateHash);
                broadcast_host_snapshot(session);
                break;
            }

            memset(&actionResult, 0, sizeof(actionResult));
            actionResult.drawnCard = DEVELOPMENT_CARD_COUNT;
            if (gameApplyAction(&session->map, &authoritativeAction, NULL, &actionResult))
            {
                matchSessionRefreshStateHash(session);
                if (session->netplay != NULL)
                {
                    netplayQueueActionResult(session->netplay, &authoritativeAction, &actionResult, session->stateHash);
                    broadcast_host_snapshot(session);
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
        apply_client_authoritative_result(&event->action, &event->result);
        break;

    case NETPLAY_EVENT_ACTION_REJECT:
        session->awaitingAuthoritativeUpdate = false;
        uiShowCenteredWarning(loc("Action rejected by host."));
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
                actions_match(&session->pendingTradeOffer, &event->action))
            {
                if (event->accepted)
                {
                    struct GameActionResult actionResult;
                    memset(&actionResult, 0, sizeof(actionResult));
                    actionResult.drawnCard = DEVELOPMENT_CARD_COUNT;
                    if (gameApplyAction(&session->map, &session->pendingTradeOffer, NULL, &actionResult))
                    {
                        matchSessionRefreshStateHash(session);
                        broadcast_host_snapshot(session);
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
        session->connectionStatus = MATCH_CONNECTION_ERROR;
        set_connection_error(session, netplayGetLastError(session->netplay));
    }

    while (netplayPollEvent(session->netplay, &event))
    {
        handle_netplay_event(session, &event);
    }
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
