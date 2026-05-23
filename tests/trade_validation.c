#include "game_action.h"
#include "game_logic.h"
#include "map_snapshot.h"
#include "match_session.h"
#include "netplay.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int gFailureCount = 0;
static int gTestCount = 0;

static bool test_declining_pending_trade_does_not_transfer_resources(void);
static bool test_authoritative_snapshot_restores_local_discard_control_after_rejoin(void);
static bool test_lobby_state_clears_started_flag_until_fresh_match_sync(void);

#define ASSERT_TRUE(expr)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __func__, __LINE__, #expr);                             \
            return false;                                                                                               \
        }                                                                                                              \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ_INT(expected, actual)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        const int expectedValue = (expected);                                                                          \
        const int actualValue = (actual);                                                                              \
        if (expectedValue != actualValue)                                                                              \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: expected %d but was %d\n", __func__, __LINE__, expectedValue, actualValue);      \
            return false;                                                                                               \
        }                                                                                                              \
    } while (0)

int main(void)
{
    const struct
    {
        const char *name;
        bool (*fn)(void);
    } tests[] = {
        {"declining a pending trade leaves resources unchanged", test_declining_pending_trade_does_not_transfer_resources},
        {"authoritative snapshot restores local discard control after rejoin", test_authoritative_snapshot_restores_local_discard_control_after_rejoin},
        {"lobby state clears started flag until fresh match sync", test_lobby_state_clears_started_flag_until_fresh_match_sync},
    };

    for (int i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    {
        gTestCount++;
        if (tests[i].fn())
        {
            printf("[PASS] %s\n", tests[i].name);
        }
        else
        {
            gFailureCount++;
            printf("[FAIL] %s\n", tests[i].name);
        }
    }

    printf("\n%d/%d trade tests passed\n", gTestCount - gFailureCount, gTestCount);
    return gFailureCount == 0 ? 0 : 1;
}

static bool test_declining_pending_trade_does_not_transfer_resources(void)
{
    struct MatchSession session;
    const int redWheatBefore = 4;
    const int blueClayBefore = 3;

    matchSessionInit(&session);
    session.netplay = netplayCreate();
    ASSERT_TRUE(session.netplay != NULL);

    session.map.phase = GAME_PHASE_PLAY;
    session.map.currentPlayer = PLAYER_RED;
    session.map.rolledThisTurn = true;
    session.map.players[PLAYER_RED].resources[RESOURCE_WHEAT] = redWheatBefore;
    session.map.players[PLAYER_BLUE].resources[RESOURCE_CLAY] = blueClayBefore;

    session.pendingTradeOfferActive = true;
    session.pendingTradeAwaitingLocalResponse = true;
    session.pendingTradeRequestedByRemote = true;
    session.pendingTradePeerId = 1;
    session.pendingTradeOffer.type = GAME_ACTION_TRADE_WITH_PLAYER;
    session.pendingTradeOffer.player = PLAYER_BLUE;
    session.pendingTradeOffer.resourceA = RESOURCE_WHEAT;
    session.pendingTradeOffer.resourceB = RESOURCE_CLAY;
    session.pendingTradeOffer.amountA = 1;
    session.pendingTradeOffer.amountB = 1;

    ASSERT_TRUE(matchSessionRespondToPendingTradeOffer(&session, false));
    ASSERT_EQ_INT(redWheatBefore, session.map.players[PLAYER_RED].resources[RESOURCE_WHEAT]);
    ASSERT_EQ_INT(blueClayBefore, session.map.players[PLAYER_BLUE].resources[RESOURCE_CLAY]);
    ASSERT_FALSE(matchSessionHasPendingTradeOfferForLocalResponse(&session));

    netplayDestroy(session.netplay);
    session.netplay = NULL;
    return true;
}

static bool test_authoritative_snapshot_restores_local_discard_control_after_rejoin(void)
{
    struct MatchSession session;
    struct Map authoritativeMap;
    unsigned char snapshot[NETPLAY_MAX_PAYLOAD_SIZE];
    size_t snapshotSize = 0u;

    matchSessionInit(&session);
    matchSessionConfigurePrivateClient(&session, PLAYER_BLUE);

    ASSERT_TRUE(setupMap(&authoritativeMap));
    authoritativeMap.phase = GAME_PHASE_PLAY;
    authoritativeMap.currentPlayer = PLAYER_RED;
    authoritativeMap.rolledThisTurn = true;
    authoritativeMap.players[PLAYER_BLUE].resources[RESOURCE_WOOD] = 1;
    authoritativeMap.players[PLAYER_BLUE].resources[RESOURCE_WHEAT] = 1;
    authoritativeMap.discardRemaining[PLAYER_BLUE] = 2;

    snapshotSize = mapSerializeSnapshot(&authoritativeMap, snapshot, sizeof(snapshot));
    ASSERT_EQ_INT((int)mapSnapshotSerializedSize(), (int)snapshotSize);
    ASSERT_TRUE(matchSessionApplyAuthoritativeSnapshot(&session, snapshot, snapshotSize));
    ASSERT_TRUE(matchSessionHasStarted(&session));
    ASSERT_TRUE(matchSessionConsumePendingMatchInitUiReset(&session));
    ASSERT_FALSE(matchSessionConsumePendingMatchInitUiReset(&session));
    ASSERT_EQ_INT(PLAYER_BLUE, gameGetCurrentDiscardPlayer(&session.map));
    ASSERT_TRUE(matchSessionLocalControlsPlayer(&session, PLAYER_BLUE));
    ASSERT_TRUE(matchSessionLocalCanActOnCurrentDecision(&session));
    return true;
}

static bool test_lobby_state_clears_started_flag_until_fresh_match_sync(void)
{
    struct MatchSession session;
    struct Map authoritativeMap;
    struct NetplayLobbyStateInfo lobbyState = {0};
    unsigned char snapshot[NETPLAY_MAX_PAYLOAD_SIZE];
    size_t snapshotSize = 0u;

    matchSessionInit(&session);
    matchSessionConfigurePrivateClient(&session, PLAYER_BLUE);

    ASSERT_TRUE(setupMap(&authoritativeMap));
    authoritativeMap.phase = GAME_PHASE_PLAY;
    authoritativeMap.currentPlayer = PLAYER_RED;
    authoritativeMap.rolledThisTurn = true;
    authoritativeMap.discardRemaining[PLAYER_BLUE] = 2;
    authoritativeMap.players[PLAYER_BLUE].resources[RESOURCE_WOOD] = 1;
    authoritativeMap.players[PLAYER_BLUE].resources[RESOURCE_WHEAT] = 1;

    snapshotSize = mapSerializeSnapshot(&authoritativeMap, snapshot, sizeof(snapshot));
    ASSERT_EQ_INT((int)mapSnapshotSerializedSize(), (int)snapshotSize);
    ASSERT_TRUE(matchSessionApplyAuthoritativeSnapshot(&session, snapshot, snapshotSize));
    ASSERT_TRUE(matchSessionHasStarted(&session));

    lobbyState.controlMode[PLAYER_RED] = PLAYER_CONTROL_HUMAN;
    lobbyState.controlMode[PLAYER_BLUE] = PLAYER_CONTROL_HUMAN;
    lobbyState.controlMode[PLAYER_GREEN] = PLAYER_CONTROL_DISABLED;
    lobbyState.controlMode[PLAYER_BLACK] = PLAYER_CONTROL_DISABLED;
    lobbyState.aiDifficulty[PLAYER_RED] = AI_DIFFICULTY_EASY;
    lobbyState.aiDifficulty[PLAYER_BLUE] = AI_DIFFICULTY_EASY;
    lobbyState.aiDifficulty[PLAYER_GREEN] = AI_DIFFICULTY_EASY;
    lobbyState.aiDifficulty[PLAYER_BLACK] = AI_DIFFICULTY_EASY;

    ASSERT_TRUE(matchSessionApplyLobbyState(&session, &lobbyState));
    ASSERT_FALSE(matchSessionHasStarted(&session));
    ASSERT_FALSE(matchSessionLocalControlsPlayer(&session, PLAYER_BLUE));
    ASSERT_FALSE(matchSessionLocalCanActOnCurrentDecision(&session));
    return true;
}
