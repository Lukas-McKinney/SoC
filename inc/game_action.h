#ifndef GAME_ACTION_H
#define GAME_ACTION_H

#include "map.h"

#include <stdbool.h>

#include <raylib.h>

enum GameActionType
{
    GAME_ACTION_NONE,
    GAME_ACTION_ROLL_DICE,
    GAME_ACTION_END_TURN,
    GAME_ACTION_SUBMIT_DISCARD,
    GAME_ACTION_MOVE_THIEF,
    GAME_ACTION_STEAL_RANDOM_RESOURCE,
    GAME_ACTION_PLACE_ROAD,
    GAME_ACTION_PLACE_SETTLEMENT,
    GAME_ACTION_PLACE_CITY,
    GAME_ACTION_BUY_DEVELOPMENT,
    GAME_ACTION_PLAY_KNIGHT,
    GAME_ACTION_PLAY_ROAD_BUILDING,
    GAME_ACTION_PLAY_YEAR_OF_PLENTY,
    GAME_ACTION_PLAY_MONOPOLY,
    GAME_ACTION_TRADE_MARITIME,
    GAME_ACTION_TRADE_WITH_PLAYER
};

struct GameActionContext
{
    Vector2 boardOrigin;
    float boardRadius;
};

struct GameAction
{
    enum GameActionType type;
    int tileId;
    int cornerIndex;
    int sideIndex;
    int diceRoll;
    enum PlayerType player;
    enum ResourceType resourceA;
    enum ResourceType resourceB;
    int amountA;
    int amountB;
    int resources[5];
};

struct GameActionResult
{
    bool applied;
    int diceRoll;
    enum DevelopmentCardType drawnCard;
    enum ResourceType stolenResource;
};

struct GameActionContext gameActionDefaultContext(void);
bool gameApplyAction(struct Map *map, const struct GameAction *action, const struct GameActionContext *context, struct GameActionResult *result);

#endif
