#include "ai_controller.h"
#include "game_action.h"
#include "game_logic.h"
#include "map_snapshot.h"
#include "match_session.h"
#include "netplay.h"
#include "renderer_internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int gFailureCount = 0;
static int gTestCount = 0;

static bool test_declining_pending_trade_does_not_transfer_resources(void);
static bool test_authoritative_snapshot_restores_local_discard_control_after_rejoin(void);
static bool test_lobby_state_clears_started_flag_until_fresh_match_sync(void);
static bool test_snapshot_hash_ignores_local_turn_timer(void);
static bool test_ai_plays_year_of_plenty_when_it_improves_position(void);
static bool test_ai_skips_monopoly_when_it_cannot_improve_position(void);

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
        {"snapshot hash ignores local turn timer", test_snapshot_hash_ignores_local_turn_timer},
        {"ai spends year of plenty when it improves position", test_ai_plays_year_of_plenty_when_it_improves_position},
        {"ai skips monopoly when it cannot improve position", test_ai_skips_monopoly_when_it_cannot_improve_position},
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

static bool test_snapshot_hash_ignores_local_turn_timer(void)
{
    struct Map mapA;
    struct Map mapB;
    struct Map restoredMap;
    unsigned char snapshot[NETPLAY_MAX_PAYLOAD_SIZE];
    size_t snapshotSize = 0u;

    ASSERT_TRUE(setupMap(&mapA));
    mapB = mapA;
    mapA.turnStartTime = 12.5;
    mapB.turnStartTime = 98.25;

    ASSERT_EQ_INT((int)mapComputeSnapshotHash(&mapA), (int)mapComputeSnapshotHash(&mapB));

    snapshotSize = mapSerializeSnapshot(&mapA, snapshot, sizeof(snapshot));
    ASSERT_EQ_INT((int)mapSnapshotSerializedSize(), (int)snapshotSize);
    ASSERT_TRUE(mapDeserializeSnapshot(&restoredMap, snapshot, snapshotSize));
    ASSERT_TRUE(restoredMap.turnStartTime == 0.0);
    ASSERT_EQ_INT((int)mapComputeSnapshotHash(&mapA), (int)mapComputeSnapshotHash(&restoredMap));
    return true;
}

static bool test_ai_plays_year_of_plenty_when_it_improves_position(void)
{
    struct Map map;
    struct GameAction action;

    ASSERT_TRUE(setupMap(&map));
    map.phase = GAME_PHASE_PLAY;
    map.currentPlayer = PLAYER_RED;
    map.setupStartPlayer = PLAYER_RED;
    map.rolledThisTurn = true;
    map.turnStartTime = 0.0;

    map.players[PLAYER_RED].controlMode = PLAYER_CONTROL_AI;
    map.players[PLAYER_RED].aiDifficulty = AI_DIFFICULTY_EASY;
    map.players[PLAYER_BLUE].controlMode = PLAYER_CONTROL_HUMAN;
    map.players[PLAYER_GREEN].controlMode = PLAYER_CONTROL_DISABLED;
    map.players[PLAYER_BLACK].controlMode = PLAYER_CONTROL_DISABLED;
    gameApplySeatControlModes(&map);

    PlaceSettlementOnSharedCorner(&map, 9, 0, PLAYER_RED, STRUCTURE_TOWN);
    gameRefreshAwards(&map);

    memset(map.players[PLAYER_RED].resources, 0, sizeof(map.players[PLAYER_RED].resources));
    memset(map.players[PLAYER_RED].developmentCards, 0, sizeof(map.players[PLAYER_RED].developmentCards));
    memset(map.players[PLAYER_RED].newlyPurchasedDevelopmentCards, 0, sizeof(map.players[PLAYER_RED].newlyPurchasedDevelopmentCards));
    map.players[PLAYER_RED].resources[RESOURCE_STONE] = 3;
    map.players[PLAYER_RED].developmentCards[DEVELOPMENT_CARD_YEAR_OF_PLENTY] = 1;
    map.playedDevelopmentCardThisTurn = false;

    ASSERT_TRUE(aiChoosePlayPhaseActionForTesting(&map, AI_DIFFICULTY_EASY, 0, 0, &action));
    ASSERT_EQ_INT(GAME_ACTION_PLAY_YEAR_OF_PLENTY, action.type);
    ASSERT_TRUE(gameApplyAction(&map, &action, NULL, NULL));
    ASSERT_EQ_INT(0, map.players[PLAYER_RED].developmentCards[DEVELOPMENT_CARD_YEAR_OF_PLENTY]);
    ASSERT_EQ_INT(5,
                  map.players[PLAYER_RED].resources[RESOURCE_WOOD] +
                      map.players[PLAYER_RED].resources[RESOURCE_WHEAT] +
                      map.players[PLAYER_RED].resources[RESOURCE_CLAY] +
                      map.players[PLAYER_RED].resources[RESOURCE_SHEEP] +
                      map.players[PLAYER_RED].resources[RESOURCE_STONE]);
    return true;
}

static bool test_ai_skips_monopoly_when_it_cannot_improve_position(void)
{
    struct Map map;
    struct GameAction action;

    ASSERT_TRUE(setupMap(&map));
    map.phase = GAME_PHASE_PLAY;
    map.currentPlayer = PLAYER_RED;
    map.setupStartPlayer = PLAYER_RED;
    map.rolledThisTurn = true;
    map.turnStartTime = 0.0;

    map.players[PLAYER_RED].controlMode = PLAYER_CONTROL_AI;
    map.players[PLAYER_RED].aiDifficulty = AI_DIFFICULTY_EASY;
    map.players[PLAYER_BLUE].controlMode = PLAYER_CONTROL_HUMAN;
    map.players[PLAYER_GREEN].controlMode = PLAYER_CONTROL_DISABLED;
    map.players[PLAYER_BLACK].controlMode = PLAYER_CONTROL_DISABLED;
    gameApplySeatControlModes(&map);

    memset(map.players[PLAYER_RED].resources, 0, sizeof(map.players[PLAYER_RED].resources));
    memset(map.players[PLAYER_BLUE].resources, 0, sizeof(map.players[PLAYER_BLUE].resources));
    memset(map.players[PLAYER_RED].developmentCards, 0, sizeof(map.players[PLAYER_RED].developmentCards));
    memset(map.players[PLAYER_RED].newlyPurchasedDevelopmentCards, 0, sizeof(map.players[PLAYER_RED].newlyPurchasedDevelopmentCards));
    map.players[PLAYER_RED].developmentCards[DEVELOPMENT_CARD_MONOPOLY] = 1;
    map.playedDevelopmentCardThisTurn = false;

    ASSERT_FALSE(aiChoosePlayPhaseActionForTesting(&map, AI_DIFFICULTY_EASY, 0, 0, &action));
    return true;
}
