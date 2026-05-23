#include "game_action.h"
#include "game_logic.h"
#include "match_session.h"
#include "netplay.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int gFailureCount = 0;
static int gTestCount = 0;

static bool test_declining_pending_trade_does_not_transfer_resources(void);

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