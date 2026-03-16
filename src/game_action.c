#include "game_action.h"

#include "board_rules.h"
#include "game_logic.h"
#include "renderer_internal.h"

#include <stdlib.h>
#include <string.h>

static struct GameActionContext resolve_context(const struct GameActionContext *context)
{
    if (context != NULL)
    {
        return *context;
    }

    return gameActionDefaultContext();
}

static void init_result(struct GameActionResult *result)
{
    if (result == NULL)
    {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->drawnCard = DEVELOPMENT_CARD_COUNT;
    result->stolenResource = RESOURCE_WOOD;
}

static bool apply_place_road(struct Map *map, const struct GameAction *action, const struct GameActionContext *context)
{
    const struct GameActionContext resolved = resolve_context(context);
    const bool setupRoadMode = gameIsSetupRoadTurn(map);
    const bool hasFreeRoadPlacements = gameHasFreeRoadPlacements(map);

    if (map == NULL || action == NULL ||
        action->tileId < 0 || action->tileId >= LAND_TILE_COUNT ||
        action->sideIndex < 0 || action->sideIndex >= HEX_CORNERS ||
        !IsCanonicalSharedEdge(action->tileId, action->sideIndex) ||
        IsSharedEdgeOccupied(map, action->tileId, action->sideIndex) ||
        !boardIsValidRoadPlacement(map, action->tileId, action->sideIndex, map->currentPlayer, resolved.boardOrigin, resolved.boardRadius) ||
        (setupRoadMode &&
         !boardEdgeTouchesCorner(action->tileId, action->sideIndex, map->setupSettlementTileId, map->setupSettlementCornerIndex, resolved.boardOrigin, resolved.boardRadius)))
    {
        return false;
    }

    if (!setupRoadMode && !hasFreeRoadPlacements && !gameTryBuyRoad(map))
    {
        return false;
    }

    PlaceRoadOnSharedEdge(map, action->tileId, action->sideIndex, map->currentPlayer);
    gameRefreshAwards(map);

    if (setupRoadMode)
    {
        gameHandlePlacedRoad(map);
    }
    else if (hasFreeRoadPlacements)
    {
        gameConsumeFreeRoadPlacement(map);
        gameCheckVictory(map, map->currentPlayer);
    }
    else
    {
        gameCheckVictory(map, map->currentPlayer);
    }

    return true;
}

static bool apply_place_settlement(struct Map *map, const struct GameAction *action, const struct GameActionContext *context)
{
    const struct GameActionContext resolved = resolve_context(context);
    const bool setupSettlementMode = gameIsSetupSettlementTurn(map);

    if (map == NULL || action == NULL ||
        action->tileId < 0 || action->tileId >= LAND_TILE_COUNT ||
        action->cornerIndex < 0 || action->cornerIndex >= HEX_CORNERS ||
        !IsCanonicalSharedCorner(action->tileId, action->cornerIndex) ||
        !boardIsValidSettlementPlacement(map, action->tileId, action->cornerIndex, map->currentPlayer, resolved.boardOrigin, resolved.boardRadius))
    {
        return false;
    }

    if (!setupSettlementMode && !gameTryBuySettlement(map))
    {
        return false;
    }

    PlaceSettlementOnSharedCorner(map, action->tileId, action->cornerIndex, map->currentPlayer, STRUCTURE_TOWN);
    gameRefreshAwards(map);
    if (setupSettlementMode)
    {
        gameHandlePlacedSettlement(map, action->tileId, action->cornerIndex);
    }
    else
    {
        gameCheckVictory(map, map->currentPlayer);
    }

    return true;
}

static bool apply_place_city(struct Map *map, const struct GameAction *action)
{
    if (map == NULL || action == NULL ||
        action->tileId < 0 || action->tileId >= LAND_TILE_COUNT ||
        action->cornerIndex < 0 || action->cornerIndex >= HEX_CORNERS ||
        !boardIsValidCityPlacement(map, action->tileId, action->cornerIndex, map->currentPlayer) ||
        !gameTryBuyCity(map))
    {
        return false;
    }

    PlaceSettlementOnSharedCorner(map, action->tileId, action->cornerIndex, map->currentPlayer, STRUCTURE_CITY);
    gameRefreshAwards(map);
    gameCheckVictory(map, map->currentPlayer);
    return true;
}

static bool apply_steal_random_resource(struct Map *map, const struct GameAction *action, struct GameActionResult *result)
{
    enum ResourceType stolenResource = RESOURCE_WOOD;

    if (map == NULL || action == NULL)
    {
        return false;
    }

    if (!gameStealRandomResourceDetailed(map, action->player, &stolenResource))
    {
        return false;
    }

    if (result != NULL)
    {
        result->stolenResource = stolenResource;
        result->applied = true;
    }
    return true;
}

struct GameActionContext gameActionDefaultContext(void)
{
    struct GameActionContext context;
    context.boardOrigin = (Vector2){
        (float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR,
        (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    context.boardRadius = BOARD_HEX_RADIUS;
    return context;
}

bool gameApplyAction(struct Map *map, const struct GameAction *action, const struct GameActionContext *context, struct GameActionResult *result)
{
    init_result(result);
    if (map == NULL || action == NULL)
    {
        return false;
    }

    if (result != NULL)
    {
        result->diceRoll = action->diceRoll;
    }

    switch (action->type)
    {
    case GAME_ACTION_ROLL_DICE:
        if (action->diceRoll < 2 || action->diceRoll > 12 || !gameCanRollDice(map))
        {
            return false;
        }
        gameRollDice(map, action->diceRoll);
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_END_TURN:
        if (!gameCanEndTurn(map))
        {
            return false;
        }
        gameEndTurn(map);
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_SUBMIT_DISCARD:
        if (!gameTrySubmitDiscard(map, action->player, action->resources))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_MOVE_THIEF:
        if (!gameCanMoveThiefToTile(map, action->tileId))
        {
            return false;
        }
        gameMoveThief(map, action->tileId);
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_STEAL_RANDOM_RESOURCE:
        if (!apply_steal_random_resource(map, action, result))
        {
            return false;
        }
        return true;

    case GAME_ACTION_PLACE_ROAD:
        if (!apply_place_road(map, action, context))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_PLACE_SETTLEMENT:
        if (!apply_place_settlement(map, action, context))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_PLACE_CITY:
        if (!apply_place_city(map, action))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_BUY_DEVELOPMENT:
        if (!gameTryBuyDevelopment(map, result != NULL ? &result->drawnCard : NULL))
        {
            return false;
        }
        gameCheckVictory(map, map->currentPlayer);
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_PLAY_KNIGHT:
        if (!gameTryPlayKnight(map))
        {
            return false;
        }
        gameCheckVictory(map, map->currentPlayer);
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_PLAY_ROAD_BUILDING:
        if (!gameTryPlayRoadBuilding(map))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_PLAY_YEAR_OF_PLENTY:
        if (!gameTryPlayYearOfPlenty(map, action->resourceA, action->resourceB))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_PLAY_MONOPOLY:
        if (!gameTryPlayMonopoly(map, action->resourceA))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_TRADE_MARITIME:
        if (!gameTryTradeMaritime(map, action->resourceA, action->amountA, action->resourceB))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_TRADE_WITH_PLAYER:
        if (!gameTryTradeWithPlayer(map, action->player, action->resourceA, action->amountA, action->resourceB, action->amountB))
        {
            return false;
        }
        if (result != NULL)
        {
            result->applied = true;
        }
        return true;

    case GAME_ACTION_NONE:
    default:
        return false;
    }
}
