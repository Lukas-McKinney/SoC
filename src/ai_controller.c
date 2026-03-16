#include "ai_controller.h"

#include "board_rules.h"
#include "game_logic.h"
#include "localization.h"
#include "renderer_internal.h"
#include "ui_state.h"

#include <float.h>
#include <math.h>
#include <raylib.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define AI_BUILD_ACTIONS_EASY 1
#define AI_BUILD_ACTIONS_MEDIUM 2
#define AI_BUILD_ACTIONS_HARD 3
#define AI_SEARCH_WIN_SCORE 100000.0f

static const int kRoadCost[5] = {1, 0, 1, 0, 0};
static const int kSettlementCost[5] = {1, 1, 1, 1, 0};
static const int kCityCost[5] = {0, 2, 0, 0, 3};
static const int kDevelopmentCost[5] = {0, 1, 0, 1, 1};

struct CornerCandidate
{
    int tileId;
    int cornerIndex;
    float score;
};

struct EdgeCandidate
{
    int tileId;
    int sideIndex;
    float score;
};

enum AiActionType
{
    AI_ACTION_NONE,
    AI_ACTION_BUILD_SETTLEMENT,
    AI_ACTION_BUILD_CITY,
    AI_ACTION_BUILD_ROAD,
    AI_ACTION_BUY_DEVELOPMENT,
    AI_ACTION_PLAY_KNIGHT,
    AI_ACTION_PLAY_ROAD_BUILDING,
    AI_ACTION_PLAY_YEAR_OF_PLENTY,
    AI_ACTION_PLAY_MONOPOLY,
    AI_ACTION_MARITIME_TRADE,
    AI_ACTION_MOVE_THIEF,
    AI_ACTION_CHOOSE_THIEF_VICTIM,
    AI_ACTION_SETUP_SETTLEMENT,
    AI_ACTION_SETUP_ROAD,
    AI_ACTION_DISCARD
};

struct AiAction
{
    enum AiActionType type;
    int tileId;
    int cornerIndex;
    int sideIndex;
    enum ResourceType resourceA;
    enum ResourceType resourceB;
    enum PlayerType player;
    int discardPlan[5];
};

struct AiSearchState
{
    int buildActionsRemaining;
    int maritimeTradesRemaining;
};

static enum PlayerType gTrackedDecisionPlayer = PLAYER_NONE;
static enum PlayerType gTrackedTurnPlayer = PLAYER_NONE;
static double gNextAiActionTime = 0.0;
static int gAiBuildActionsThisTurn = 0;
static int gAiMaritimeTradesThisTurn = 0;
static bool gAiUiPreparedForTurn = false;

static bool player_is_ai(const struct Map *map, enum PlayerType player);
static enum PlayerType active_decision_player(const struct Map *map);
static enum AiDifficulty active_ai_difficulty(const struct Map *map, enum PlayerType player);
static double ai_delay_for_difficulty(enum AiDifficulty difficulty);
static int ai_build_action_budget(enum AiDifficulty difficulty);
static void schedule_next_ai_action(enum AiDifficulty difficulty);
static void reset_ai_turn_state(void);
static void prepare_ui_for_ai_turn(void);
static bool ai_is_blocked_by_ui(void);
static bool finalize_ai_victory(struct Map *map);

static int min_int(int a, int b);
static int max_int(int a, int b);
static int resource_total_for_player(const struct Map *map, enum PlayerType player);
static bool can_afford_cost(const int resources[5], const int cost[5]);
static float cost_progress_score(const int resources[5], const int cost[5], float readyBonus, float coveredWeight, float missingPenalty);
static float evaluate_hand_value(const struct Map *map, enum PlayerType player, const int resources[5], enum AiDifficulty difficulty);
static int dice_weight(int number);
static enum ResourceType tile_resource_type(enum TileType type);
static float resource_weight(enum ResourceType resource, enum AiDifficulty difficulty);
static bool find_corner_key(int tileId, int cornerIndex, int *x, int *y);
static float evaluate_corner_value(const struct Map *map, int tileId, int cornerIndex, enum AiDifficulty difficulty);
static bool shared_corner_occupied(const struct Map *map, int tileId, int cornerIndex);
static bool corner_has_adjacent_structure(const struct Map *map, int tileId, int cornerIndex);
static bool corner_can_host_future_settlement(const struct Map *map, int tileId, int cornerIndex);
static int count_player_roads_touching_corner(const struct Map *map, enum PlayerType player, int tileId, int cornerIndex);
static float evaluate_road_candidate(const struct Map *map, int tileId, int sideIndex, enum PlayerType player, enum AiDifficulty difficulty);
static bool find_best_settlement_candidate(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct CornerCandidate *candidate);
static bool find_best_city_candidate(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct CornerCandidate *candidate);
static bool find_best_road_candidate(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, bool requireSetupTouch, struct EdgeCandidate *candidate);
static int ai_maritime_trade_budget(enum AiDifficulty difficulty);
static float evaluate_player_strength(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty);
static float evaluate_player_position(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty);
static float evaluate_post_thief_resolution(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty);
static bool simulate_specific_steal(struct Map *map, enum PlayerType victim, enum ResourceType resource);
static float search_turn_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiSearchState state, struct AiAction *bestAction);
static float search_free_road_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiSearchState state, struct AiAction *bestAction);
static float search_thief_move_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiSearchState state, struct AiAction *bestAction);
static float search_thief_victim_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiSearchState state, struct AiAction *bestAction);
static bool find_best_play_phase_action(const struct Map *map, enum AiDifficulty difficulty, struct AiAction *action);
static bool execute_ai_action(struct Map *map, const struct AiAction *action);
static void search_best_discard_plan_recursive(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, int resourceIndex, int remaining, int discardPlan[5], float *bestScore, int bestPlan[5]);
static bool choose_best_discard_action(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiAction *action);
static float search_setup_road_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiAction *bestAction);
static bool choose_best_setup_settlement_action(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiAction *action);
static bool choose_best_setup_road_action(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiAction *action);
static bool choose_best_thief_move_action(const struct Map *map, enum AiDifficulty difficulty, struct AiAction *action);
static bool choose_best_thief_victim_action(const struct Map *map, enum AiDifficulty difficulty, struct AiAction *action);
static bool choose_best_free_road_action(const struct Map *map, enum AiDifficulty difficulty, struct AiAction *action);

static bool handle_ai_discard(struct Map *map, enum PlayerType player, enum AiDifficulty difficulty);
static bool handle_ai_thief_victim(struct Map *map, enum AiDifficulty difficulty);
static bool handle_ai_thief_move(struct Map *map, enum AiDifficulty difficulty);
static bool handle_ai_setup_settlement(struct Map *map, enum AiDifficulty difficulty);
static bool handle_ai_setup_road(struct Map *map, enum AiDifficulty difficulty);
static bool handle_ai_roll(struct Map *map);

void aiResetController(void)
{
    gTrackedDecisionPlayer = PLAYER_NONE;
    gTrackedTurnPlayer = PLAYER_NONE;
    gNextAiActionTime = 0.0;
    reset_ai_turn_state();
}

void aiConfigureHotseatMatch(struct Map *map)
{
    if (map == NULL)
    {
        return;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        map->players[player].controlMode = PLAYER_CONTROL_HUMAN;
        map->players[player].aiDifficulty = AI_DIFFICULTY_EASY;
    }
}

void aiConfigureAIMatch(struct Map *map, enum PlayerType humanPlayer, enum AiDifficulty difficulty)
{
    const enum PlayerType localHuman = humanPlayer >= PLAYER_RED && humanPlayer <= PLAYER_BLACK ? humanPlayer : PLAYER_RED;

    if (map == NULL)
    {
        return;
    }

    aiConfigureHotseatMatch(map);
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (player == localHuman)
        {
            continue;
        }

        map->players[player].controlMode = PLAYER_CONTROL_AI;
        map->players[player].aiDifficulty = difficulty;
    }
}

bool aiControlsActiveDecision(const struct Map *map)
{
    return map != NULL &&
           !gameHasWinner(map) &&
           player_is_ai(map, active_decision_player(map));
}

const char *aiDifficultyLabel(enum AiDifficulty difficulty)
{
    return locAiDifficultyLabel(difficulty);
}

void aiUpdateTurn(struct Map *map)
{
    enum PlayerType decisionPlayer;
    enum AiDifficulty difficulty;

    if (map == NULL || gameHasWinner(map))
    {
        return;
    }

    decisionPlayer = active_decision_player(map);
    if (!player_is_ai(map, decisionPlayer))
    {
        return;
    }

    difficulty = active_ai_difficulty(map, decisionPlayer);
    if (decisionPlayer != gTrackedDecisionPlayer || map->currentPlayer != gTrackedTurnPlayer)
    {
        gTrackedDecisionPlayer = decisionPlayer;
        gTrackedTurnPlayer = map->currentPlayer;
        gNextAiActionTime = GetTime() + ai_delay_for_difficulty(difficulty) * 0.6;
        reset_ai_turn_state();
    }

    if (player_is_ai(map, map->currentPlayer) && !gAiUiPreparedForTurn)
    {
        prepare_ui_for_ai_turn();
        gAiUiPreparedForTurn = true;
    }

    if (ai_is_blocked_by_ui() || GetTime() < gNextAiActionTime)
    {
        return;
    }

    if (gameHasPendingDiscards(map))
    {
        if (handle_ai_discard(map, decisionPlayer, difficulty))
        {
            schedule_next_ai_action(difficulty);
        }
        return;
    }

    if (gameNeedsThiefVictimSelection(map))
    {
        if (handle_ai_thief_victim(map, difficulty))
        {
            schedule_next_ai_action(difficulty);
        }
        return;
    }

    if (gameNeedsThiefPlacement(map))
    {
        if (handle_ai_thief_move(map, difficulty))
        {
            schedule_next_ai_action(difficulty);
        }
        return;
    }

    if (gameIsSetupSettlementTurn(map))
    {
        if (handle_ai_setup_settlement(map, difficulty))
        {
            schedule_next_ai_action(difficulty);
        }
        return;
    }

    if (gameIsSetupRoadTurn(map))
    {
        if (handle_ai_setup_road(map, difficulty))
        {
            schedule_next_ai_action(difficulty);
        }
        return;
    }

    if (map->phase != GAME_PHASE_PLAY)
    {
        return;
    }

    if (!map->rolledThisTurn)
    {
        if (handle_ai_roll(map))
        {
            schedule_next_ai_action(difficulty);
        }
        return;
    }

    if (gameHasFreeRoadPlacements(map))
    {
        struct AiAction action;
        if (choose_best_free_road_action(map, difficulty, &action) &&
            execute_ai_action(map, &action))
        {
            schedule_next_ai_action(difficulty);
        }
        else
        {
            map->freeRoadPlacementsRemaining = 0;
            schedule_next_ai_action(difficulty);
        }
        return;
    }

    {
        struct AiAction action;
        if (find_best_play_phase_action(map, difficulty, &action) &&
            execute_ai_action(map, &action))
        {
            if (action.type == AI_ACTION_MARITIME_TRADE)
            {
                gAiMaritimeTradesThisTurn++;
            }
            else if (action.type == AI_ACTION_BUILD_SETTLEMENT ||
                     action.type == AI_ACTION_BUILD_CITY ||
                     action.type == AI_ACTION_BUILD_ROAD ||
                     action.type == AI_ACTION_BUY_DEVELOPMENT)
            {
                gAiBuildActionsThisTurn++;
            }
            schedule_next_ai_action(difficulty);
            return;
        }
    }

    if (gameCanEndTurn(map))
    {
        gameEndTurn(map);
        gTrackedDecisionPlayer = active_decision_player(map);
        gTrackedTurnPlayer = map->currentPlayer;
        reset_ai_turn_state();
    }
}

bool aiShouldAcceptPlayerTradeOffer(const struct Map *map, enum PlayerType aiPlayer, enum ResourceType give, int giveAmount, enum ResourceType receive, int receiveAmount)
{
    float beforeScore;
    float afterScore;
    float threshold;
    const enum AiDifficulty difficulty = active_ai_difficulty(map, aiPlayer);
    const int aiVisiblePoints = gameComputeVisibleVictoryPoints(map, aiPlayer);
    const int offeringPlayerPoints = gameComputeVisibleVictoryPoints(map, map != NULL ? map->currentPlayer : PLAYER_NONE);
    struct Map simulatedMap;

    if (map == NULL ||
        aiPlayer < PLAYER_RED || aiPlayer > PLAYER_BLACK ||
        !player_is_ai(map, aiPlayer) ||
        !gameCanTradeWithPlayer(map, aiPlayer, give, giveAmount, receive, receiveAmount))
    {
        return false;
    }

    simulatedMap = *map;
    simulatedMap.players[aiPlayer].resources[receive] -= receiveAmount;
    simulatedMap.players[aiPlayer].resources[give] += giveAmount;
    simulatedMap.players[map->currentPlayer].resources[give] -= giveAmount;
    simulatedMap.players[map->currentPlayer].resources[receive] += receiveAmount;

    beforeScore = evaluate_player_position(map, aiPlayer, difficulty);
    afterScore = evaluate_player_position(&simulatedMap, aiPlayer, difficulty);

    threshold = -0.55f;
    if (difficulty == AI_DIFFICULTY_MEDIUM)
    {
        threshold = 0.35f;
    }
    else if (difficulty == AI_DIFFICULTY_HARD)
    {
        threshold = 1.05f;
        if (offeringPlayerPoints >= aiVisiblePoints)
        {
            threshold += 0.55f;
        }
        if (offeringPlayerPoints >= 8)
        {
            threshold += 0.45f;
        }
    }

    return (afterScore - beforeScore) >= threshold;
}

static bool player_is_ai(const struct Map *map, enum PlayerType player)
{
    return map != NULL &&
           player >= PLAYER_RED &&
           player <= PLAYER_BLACK &&
           map->players[player].controlMode == PLAYER_CONTROL_AI;
}

static enum PlayerType active_decision_player(const struct Map *map)
{
    if (map == NULL)
    {
        return PLAYER_NONE;
    }
    if (gameHasPendingDiscards(map))
    {
        return gameGetCurrentDiscardPlayer(map);
    }
    return map->currentPlayer;
}

static enum AiDifficulty active_ai_difficulty(const struct Map *map, enum PlayerType player)
{
    if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return AI_DIFFICULTY_EASY;
    }
    return map->players[player].aiDifficulty;
}

static double ai_delay_for_difficulty(enum AiDifficulty difficulty)
{
    const double speedNormalized = (double)uiGetAiSpeedSetting() / 10.0;
    double baseDelay = 1.35;

    if (difficulty == AI_DIFFICULTY_MEDIUM)
    {
        baseDelay = 1.10;
    }
    else if (difficulty == AI_DIFFICULTY_HARD)
    {
        baseDelay = 0.92;
    }

    return baseDelay * (1.0 - speedNormalized);
}

static int ai_build_action_budget(enum AiDifficulty difficulty)
{
    switch (difficulty)
    {
    case AI_DIFFICULTY_HARD:
        return AI_BUILD_ACTIONS_HARD;
    case AI_DIFFICULTY_MEDIUM:
        return AI_BUILD_ACTIONS_MEDIUM;
    case AI_DIFFICULTY_EASY:
    default:
        return AI_BUILD_ACTIONS_EASY;
    }
}

static void schedule_next_ai_action(enum AiDifficulty difficulty)
{
    gNextAiActionTime = GetTime() + ai_delay_for_difficulty(difficulty);
}

static void reset_ai_turn_state(void)
{
    gAiBuildActionsThisTurn = 0;
    gAiMaritimeTradesThisTurn = 0;
    gAiUiPreparedForTurn = false;
}

static void prepare_ui_for_ai_turn(void)
{
    uiSetBuildPanelOpen(false);
    uiSetTradeMenuOpen(false);
    uiSetPlayerTradeMenuOpen(false);
    uiSetDevelopmentPurchaseConfirmOpen(false);
    uiSetDevelopmentPlayConfirmOpen(false);
}

static bool ai_is_blocked_by_ui(void)
{
    return uiIsSettingsMenuOpen() ||
           uiIsBoardCreationAnimating() ||
           uiGetBoardUiFadeProgress() < 1.0f ||
           uiIsDiceRolling() ||
           uiIsDevelopmentCardDrawAnimating() ||
           uiIsDevelopmentPurchaseConfirmOpen() ||
           uiIsDevelopmentPlayConfirmOpen();
}

static bool finalize_ai_victory(struct Map *map)
{
    if (!gameCheckVictory(map, map->currentPlayer))
    {
        return false;
    }

    prepare_ui_for_ai_turn();
    return true;
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static int resource_total_for_player(const struct Map *map, enum PlayerType player)
{
    int total = 0;
    if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return 0;
    }

    for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
    {
        total += map->players[player].resources[resource];
    }
    return total;
}

static bool can_afford_cost(const int resources[5], const int cost[5])
{
    if (resources == NULL || cost == NULL)
    {
        return false;
    }

    for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
    {
        if (resources[resource] < cost[resource])
        {
            return false;
        }
    }

    return true;
}

static float cost_progress_score(const int resources[5], const int cost[5], float readyBonus, float coveredWeight, float missingPenalty)
{
    int covered = 0;
    int missing = 0;
    float score = 0.0f;

    if (resources == NULL || cost == NULL)
    {
        return -FLT_MAX;
    }

    for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
    {
        covered += min_int(resources[resource], cost[resource]);
        missing += max_int(cost[resource] - resources[resource], 0);
    }

    score = (float)covered * coveredWeight - (float)missing * missingPenalty;
    if (missing == 0)
    {
        score += readyBonus;
    }
    return score;
}

static float evaluate_hand_value(const struct Map *map, enum PlayerType player, const int resources[5], enum AiDifficulty difficulty)
{
    float score = 0.0f;
    float cityReadyBonus = 7.2f;

    if (resources == NULL)
    {
        return -FLT_MAX;
    }

    for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
    {
        const int amount = resources[resource];
        const int capped = amount < 5 ? amount : 5;
        score += (float)capped * resource_weight((enum ResourceType)resource, difficulty) * 0.55f;
        if (amount > capped)
        {
            score += (float)(amount - capped) * 0.14f;
        }
    }

    if (map != NULL && player >= PLAYER_RED && player <= PLAYER_BLACK && gameComputeVictoryPoints(map, player) >= 8)
    {
        cityReadyBonus += 1.5f;
    }

    score += cost_progress_score(resources, kSettlementCost, 6.0f, 1.15f, 1.45f);
    score += cost_progress_score(resources, kCityCost, cityReadyBonus, 1.30f, 1.70f);
    score += cost_progress_score(resources, kRoadCost, 2.2f, 0.80f, 1.00f);

    if (map != NULL && gameGetDevelopmentDeckCount(map) > 0)
    {
        score += cost_progress_score(resources, kDevelopmentCost, 3.2f, 0.95f, 1.15f);
    }

    if (difficulty == AI_DIFFICULTY_HARD && can_afford_cost(resources, kSettlementCost) && can_afford_cost(resources, kRoadCost))
    {
        score += 1.2f;
    }

    return score;
}

static int dice_weight(int number)
{
    switch (number)
    {
    case 6:
    case 8:
        return 5;
    case 5:
    case 9:
        return 4;
    case 4:
    case 10:
        return 3;
    case 3:
    case 11:
        return 2;
    case 2:
    case 12:
        return 1;
    default:
        return 0;
    }
}

static enum ResourceType tile_resource_type(enum TileType type)
{
    switch (type)
    {
    case TILE_FOREST:
        return RESOURCE_WOOD;
    case TILE_FARMLAND:
        return RESOURCE_WHEAT;
    case TILE_MINE:
        return RESOURCE_CLAY;
    case TILE_SHEEPMEADOW:
        return RESOURCE_SHEEP;
    case TILE_MOUNTAIN:
        return RESOURCE_STONE;
    case TILE_DESERT:
    default:
        return RESOURCE_WOOD;
    }
}

static float resource_weight(enum ResourceType resource, enum AiDifficulty difficulty)
{
    if (difficulty != AI_DIFFICULTY_HARD)
    {
        return difficulty == AI_DIFFICULTY_MEDIUM ? 1.05f : 1.0f;
    }

    switch (resource)
    {
    case RESOURCE_WHEAT:
        return 1.22f;
    case RESOURCE_STONE:
        return 1.28f;
    case RESOURCE_WOOD:
        return 1.08f;
    case RESOURCE_CLAY:
        return 1.06f;
    case RESOURCE_SHEEP:
    default:
        return 1.0f;
    }
}

static bool find_corner_key(int tileId, int cornerIndex, int *x, int *y)
{
    Vector2 center;
    Vector2 origin;

    if (tileId < 0 || tileId >= LAND_TILE_COUNT || cornerIndex < 0 || cornerIndex >= HEX_CORNERS)
    {
        return false;
    }

    origin = (Vector2){(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    center = AxialToWorld(kLandCoords[tileId], origin, BOARD_HEX_RADIUS);
    RendererGetCornerKey(center, BOARD_HEX_RADIUS, cornerIndex, x, y);
    return true;
}

static float evaluate_corner_value(const struct Map *map, int tileId, int cornerIndex, enum AiDifficulty difficulty)
{
    int tx = 0;
    int ty = 0;
    int distinctResources[5] = {0};
    float score = 0.0f;
    Vector2 origin;

    if (map == NULL || !find_corner_key(tileId, cornerIndex, &tx, &ty))
    {
        return -FLT_MAX;
    }

    origin = (Vector2){(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        Vector2 center = AxialToWorld(kLandCoords[otherTile], origin, BOARD_HEX_RADIUS);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int ox = 0;
            int oy = 0;
            RendererGetCornerKey(center, BOARD_HEX_RADIUS, otherCorner, &ox, &oy);
            if (ox != tx || oy != ty)
            {
                continue;
            }

            if (map->tiles[otherTile].type == TILE_DESERT)
            {
                score += 0.2f;
                break;
            }

            {
                const enum ResourceType resource = tile_resource_type(map->tiles[otherTile].type);
                float tileScore = (float)dice_weight(map->tiles[otherTile].diceNumber) * resource_weight(resource, difficulty);
                if (map->thiefTileId == otherTile)
                {
                    tileScore *= 0.35f;
                }
                distinctResources[resource] = 1;
                score += tileScore;
            }
            break;
        }
    }

    for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
    {
        if (distinctResources[resource] != 0)
        {
            score += difficulty == AI_DIFFICULTY_HARD ? 0.75f : 0.35f;
        }
    }

    return score;
}

static bool shared_corner_occupied(const struct Map *map, int tileId, int cornerIndex)
{
    int tx = 0;
    int ty = 0;
    Vector2 origin;

    if (map == NULL || !find_corner_key(tileId, cornerIndex, &tx, &ty))
    {
        return false;
    }

    origin = (Vector2){(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        Vector2 center = AxialToWorld(kLandCoords[otherTile], origin, BOARD_HEX_RADIUS);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int ox = 0;
            int oy = 0;
            RendererGetCornerKey(center, BOARD_HEX_RADIUS, otherCorner, &ox, &oy);
            if (ox == tx && oy == ty && map->tiles[otherTile].corners[otherCorner].structure != STRUCTURE_NONE)
            {
                return true;
            }
        }
    }

    return false;
}

static bool corner_has_adjacent_structure(const struct Map *map, int tileId, int cornerIndex)
{
    int tx = 0;
    int ty = 0;
    Vector2 origin;
    Vector2 target;
    Vector2 center;

    if (map == NULL || !find_corner_key(tileId, cornerIndex, &tx, &ty))
    {
        return false;
    }

    origin = (Vector2){(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    center = AxialToWorld(kLandCoords[tileId], origin, BOARD_HEX_RADIUS);
    target = PointOnHex(center, BOARD_HEX_RADIUS, cornerIndex);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        Vector2 otherCenter = AxialToWorld(kLandCoords[otherTile], origin, BOARD_HEX_RADIUS);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int ox = 0;
            int oy = 0;
            Vector2 otherPoint;
            float dx;
            float dy;
            float distance;

            if (map->tiles[otherTile].corners[otherCorner].structure == STRUCTURE_NONE)
            {
                continue;
            }

            RendererGetCornerKey(otherCenter, BOARD_HEX_RADIUS, otherCorner, &ox, &oy);
            if (ox == tx && oy == ty)
            {
                continue;
            }

            otherPoint = PointOnHex(otherCenter, BOARD_HEX_RADIUS, otherCorner);
            dx = target.x - otherPoint.x;
            dy = target.y - otherPoint.y;
            distance = sqrtf(dx * dx + dy * dy);
            if (distance < BOARD_HEX_RADIUS * 1.05f)
            {
                return true;
            }
        }
    }

    return false;
}

static bool corner_can_host_future_settlement(const struct Map *map, int tileId, int cornerIndex)
{
    return !shared_corner_occupied(map, tileId, cornerIndex) &&
           !corner_has_adjacent_structure(map, tileId, cornerIndex);
}

static int count_player_roads_touching_corner(const struct Map *map, enum PlayerType player, int tileId, int cornerIndex)
{
    int count = 0;
    int tx = 0;
    int ty = 0;
    Vector2 origin;

    if (map == NULL || !find_corner_key(tileId, cornerIndex, &tx, &ty))
    {
        return 0;
    }

    origin = (Vector2){(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        Vector2 center = AxialToWorld(kLandCoords[otherTile], origin, BOARD_HEX_RADIUS);
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            int ax = 0;
            int ay = 0;
            int bx = 0;
            int by = 0;
            if (!map->tiles[otherTile].sides[sideIndex].isset ||
                map->tiles[otherTile].sides[sideIndex].player != player)
            {
                continue;
            }

            RendererGetRoadEdgeKey(center, BOARD_HEX_RADIUS, sideIndex, &ax, &ay, &bx, &by);
            if ((ax == tx && ay == ty) || (bx == tx && by == ty))
            {
                count++;
            }
        }
    }

    return count;
}

static float evaluate_road_candidate(const struct Map *map, int tileId, int sideIndex, enum PlayerType player, enum AiDifficulty difficulty)
{
    int cornerA = 0;
    int cornerB = 1;
    float score = 0.0f;

    GetSideCornerIndices(sideIndex, &cornerA, &cornerB);
    if (corner_can_host_future_settlement(map, tileId, cornerA))
    {
        score += evaluate_corner_value(map, tileId, cornerA, difficulty) * 0.9f;
    }
    else if (map->tiles[tileId].corners[cornerA].owner == player)
    {
        score += 1.0f + 0.4f * count_player_roads_touching_corner(map, player, tileId, cornerA);
    }

    if (corner_can_host_future_settlement(map, tileId, cornerB))
    {
        score += evaluate_corner_value(map, tileId, cornerB, difficulty) * 0.9f;
    }
    else if (map->tiles[tileId].corners[cornerB].owner == player)
    {
        score += 1.0f + 0.4f * count_player_roads_touching_corner(map, player, tileId, cornerB);
    }

    if (difficulty == AI_DIFFICULTY_HARD && gameGetLongestRoadOwner(map) != player)
    {
        score += 0.8f;
    }

    return score;
}

static bool find_best_settlement_candidate(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct CornerCandidate *candidate)
{
    bool found = false;
    if (candidate == NULL)
    {
        return false;
    }

    candidate->score = -FLT_MAX;
    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            float score;
            if (!IsCanonicalSharedCorner(tileId, cornerIndex) ||
                !boardIsValidSettlementPlacement(map, tileId, cornerIndex, player,
                                                 (Vector2){(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR},
                                                 BOARD_HEX_RADIUS))
            {
                continue;
            }

            score = evaluate_corner_value(map, tileId, cornerIndex, difficulty);
            if (!found || score > candidate->score)
            {
                candidate->tileId = tileId;
                candidate->cornerIndex = cornerIndex;
                candidate->score = score;
                found = true;
            }
        }
    }

    return found;
}

static bool find_best_city_candidate(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct CornerCandidate *candidate)
{
    bool found = false;
    if (candidate == NULL)
    {
        return false;
    }

    candidate->score = -FLT_MAX;
    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            float score;
            if (!IsCanonicalSharedCorner(tileId, cornerIndex) ||
                !boardIsValidCityPlacement(map, tileId, cornerIndex, player))
            {
                continue;
            }

            score = evaluate_corner_value(map, tileId, cornerIndex, difficulty) * 1.35f;
            if (!found || score > candidate->score)
            {
                candidate->tileId = tileId;
                candidate->cornerIndex = cornerIndex;
                candidate->score = score;
                found = true;
            }
        }
    }

    return found;
}

static bool find_best_road_candidate(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, bool requireSetupTouch, struct EdgeCandidate *candidate)
{
    bool found = false;
    Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};

    if (candidate == NULL)
    {
        return false;
    }

    candidate->score = -FLT_MAX;
    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            float score;
            if (!IsCanonicalSharedEdge(tileId, sideIndex) ||
                IsSharedEdgeOccupied(map, tileId, sideIndex) ||
                !boardIsValidRoadPlacement(map, tileId, sideIndex, player, origin, BOARD_HEX_RADIUS))
            {
                continue;
            }

            if (requireSetupTouch &&
                !boardEdgeTouchesCorner(tileId, sideIndex, map->setupSettlementTileId, map->setupSettlementCornerIndex, origin, BOARD_HEX_RADIUS))
            {
                continue;
            }

            score = evaluate_road_candidate(map, tileId, sideIndex, player, difficulty);
            if (!found || score > candidate->score)
            {
                candidate->tileId = tileId;
                candidate->sideIndex = sideIndex;
                candidate->score = score;
                found = true;
            }
        }
    }

    return found;
}

static bool handle_ai_discard(struct Map *map, enum PlayerType player, enum AiDifficulty difficulty)
{
    struct AiAction action;

    if (!choose_best_discard_action(map, player, difficulty, &action))
    {
        return false;
    }

    return execute_ai_action(map, &action);
}

static bool handle_ai_thief_victim(struct Map *map, enum AiDifficulty difficulty)
{
    struct AiAction action;

    if (!choose_best_thief_victim_action(map, difficulty, &action))
    {
        return false;
    }

    return execute_ai_action(map, &action);
}

static bool handle_ai_thief_move(struct Map *map, enum AiDifficulty difficulty)
{
    struct AiAction action;

    if (!choose_best_thief_move_action(map, difficulty, &action))
    {
        return false;
    }

    return execute_ai_action(map, &action);
}

static bool handle_ai_setup_settlement(struct Map *map, enum AiDifficulty difficulty)
{
    struct AiAction action;

    if (!choose_best_setup_settlement_action(map, map != NULL ? map->currentPlayer : PLAYER_NONE, difficulty, &action))
    {
        return false;
    }

    return execute_ai_action(map, &action);
}

static bool handle_ai_setup_road(struct Map *map, enum AiDifficulty difficulty)
{
    struct AiAction action;

    if (!choose_best_setup_road_action(map, map != NULL ? map->currentPlayer : PLAYER_NONE, difficulty, &action))
    {
        return false;
    }

    return execute_ai_action(map, &action);
}

static bool handle_ai_roll(struct Map *map)
{
    if (map == NULL || !gameCanRollDice(map) || uiIsDiceRolling())
    {
        return false;
    }

    if (uiGetAiSpeedSetting() >= 10)
    {
        gameRollDice(map, GetRandomValue(1, 6) + GetRandomValue(1, 6));
        return true;
    }

    uiStartDiceRollAnimation();
    return true;
}

static int ai_maritime_trade_budget(enum AiDifficulty difficulty)
{
    switch (difficulty)
    {
    case AI_DIFFICULTY_HARD:
        return 2;
    case AI_DIFFICULTY_MEDIUM:
        return 1;
    case AI_DIFFICULTY_EASY:
    default:
        return 0;
    }
}

static float evaluate_player_strength(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty)
{
    struct CornerCandidate settlementCandidate;
    struct CornerCandidate cityCandidate;
    struct EdgeCandidate roadCandidate;
    float score = 0.0f;
    int totalResources;

    if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return -AI_SEARCH_WIN_SCORE;
    }

    totalResources = resource_total_for_player(map, player);
    score += (float)gameComputeVictoryPoints(map, player) * 140.0f;
    score += (float)gameComputeVisibleVictoryPoints(map, player) * 22.0f;
    score += evaluate_hand_value(map, player, map->players[player].resources, difficulty) * 4.2f;

    if (find_best_city_candidate(map, player, difficulty, &cityCandidate))
    {
        score += cityCandidate.score * 2.7f;
    }
    if (find_best_settlement_candidate(map, player, difficulty, &settlementCandidate))
    {
        score += settlementCandidate.score * 2.0f;
    }
    if (find_best_road_candidate(map, player, difficulty, false, &roadCandidate))
    {
        score += roadCandidate.score * 0.85f;
    }

    score += (float)gameGetDevelopmentCardCount(map, player, DEVELOPMENT_CARD_KNIGHT) * 2.8f;
    score += (float)gameGetDevelopmentCardCount(map, player, DEVELOPMENT_CARD_ROAD_BUILDING) * 4.6f;
    score += (float)gameGetDevelopmentCardCount(map, player, DEVELOPMENT_CARD_YEAR_OF_PLENTY) * 4.2f;
    score += (float)gameGetDevelopmentCardCount(map, player, DEVELOPMENT_CARD_MONOPOLY) * 4.9f;
    score += (float)map->players[player].playedKnightCount * 1.3f;

    if (gameGetLargestArmyOwner(map) == player)
    {
        score += 22.0f;
    }
    if (gameGetLongestRoadOwner(map) == player)
    {
        score += 24.0f + (float)gameGetLongestRoadLength(map) * 1.8f;
    }

    score -= (float)max_int(totalResources - 7, 0) * 1.1f;
    return score;
}

static float evaluate_player_position(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty)
{
    float ownScore;
    float strongestOpponent = 0.0f;
    float averageOpponent = 0.0f;
    int opponentCount = 0;

    if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return -AI_SEARCH_WIN_SCORE;
    }

    if (gameHasWinner(map))
    {
        return gameGetWinner(map) == player ? AI_SEARCH_WIN_SCORE : -AI_SEARCH_WIN_SCORE;
    }

    ownScore = evaluate_player_strength(map, player, difficulty);
    for (int other = PLAYER_RED; other <= PLAYER_BLACK; other++)
    {
        float opponentScore;
        if (other == player)
        {
            continue;
        }

        opponentScore = evaluate_player_strength(map, (enum PlayerType)other, difficulty);
        if (opponentCount == 0 || opponentScore > strongestOpponent)
        {
            strongestOpponent = opponentScore;
        }
        averageOpponent += opponentScore;
        opponentCount++;
    }

    if (opponentCount > 0)
    {
        averageOpponent /= (float)opponentCount;
    }

    return ownScore - strongestOpponent * 0.72f - averageOpponent * 0.18f;
}

static float evaluate_post_thief_resolution(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty)
{
    if (map == NULL)
    {
        return -AI_SEARCH_WIN_SCORE;
    }

    if (gameNeedsThiefVictimSelection(map))
    {
        return search_thief_victim_score(map, player, difficulty, (struct AiSearchState){0}, NULL);
    }

    return evaluate_player_position(map, player, difficulty);
}

static bool simulate_specific_steal(struct Map *map, enum PlayerType victim, enum ResourceType resource)
{
    if (map == NULL ||
        !gameCanStealFromPlayer(map, victim) ||
        resource < RESOURCE_WOOD || resource > RESOURCE_STONE ||
        map->players[victim].resources[resource] <= 0)
    {
        return false;
    }

    map->players[victim].resources[resource]--;
    map->players[map->currentPlayer].resources[resource]++;
    map->awaitingThiefVictimSelection = false;
    return true;
}

static float search_thief_victim_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiSearchState state, struct AiAction *bestAction)
{
    const float epsilon = 0.001f;
    float bestScore = -AI_SEARCH_WIN_SCORE;
    bool found = false;
    (void)state;

    if (bestAction != NULL)
    {
        memset(bestAction, 0, sizeof(*bestAction));
        bestAction->type = AI_ACTION_NONE;
    }

    if (map == NULL || !gameNeedsThiefVictimSelection(map))
    {
        return evaluate_player_position(map, player, difficulty);
    }

    for (int other = PLAYER_RED; other <= PLAYER_BLACK; other++)
    {
        enum PlayerType victim = (enum PlayerType)other;
        float totalScore = 0.0f;
        int outcomeCount = 0;

        if (!gameCanStealFromPlayer(map, victim))
        {
            continue;
        }

        for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
        {
            struct Map simulatedMap;
            if (map->players[victim].resources[resource] <= 0)
            {
                continue;
            }

            simulatedMap = *map;
            if (!simulate_specific_steal(&simulatedMap, victim, (enum ResourceType)resource))
            {
                continue;
            }

            totalScore += evaluate_player_position(&simulatedMap, player, difficulty);
            outcomeCount++;
        }

        if (outcomeCount > 0)
        {
            const float score = totalScore / (float)outcomeCount;
            if (!found || score > bestScore + epsilon)
            {
                found = true;
                bestScore = score;
                if (bestAction != NULL)
                {
                    bestAction->type = AI_ACTION_CHOOSE_THIEF_VICTIM;
                    bestAction->player = victim;
                }
            }
        }
    }

    return found ? bestScore : evaluate_player_position(map, player, difficulty);
}

static float search_thief_move_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiSearchState state, struct AiAction *bestAction)
{
    const float epsilon = 0.001f;
    float bestScore = -AI_SEARCH_WIN_SCORE;
    bool found = false;
    (void)state;

    if (bestAction != NULL)
    {
        memset(bestAction, 0, sizeof(*bestAction));
        bestAction->type = AI_ACTION_NONE;
    }

    if (map == NULL || !gameNeedsThiefPlacement(map))
    {
        return evaluate_player_position(map, player, difficulty);
    }

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        struct Map simulatedMap;
        float score;

        if (!gameCanMoveThiefToTile(map, tileId))
        {
            continue;
        }

        simulatedMap = *map;
        gameMoveThief(&simulatedMap, tileId);
        score = evaluate_post_thief_resolution(&simulatedMap, player, difficulty);
        if (!found || score > bestScore + epsilon)
        {
            found = true;
            bestScore = score;
            if (bestAction != NULL)
            {
                bestAction->type = AI_ACTION_MOVE_THIEF;
                bestAction->tileId = tileId;
            }
        }
    }

    return found ? bestScore : evaluate_player_position(map, player, difficulty);
}

static float search_free_road_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiSearchState state, struct AiAction *bestAction)
{
    const float epsilon = 0.001f;
    Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    struct Map skippedMap;
    float bestScore;
    bool found = false;

    if (bestAction != NULL)
    {
        memset(bestAction, 0, sizeof(*bestAction));
        bestAction->type = AI_ACTION_NONE;
    }

    if (map == NULL || !gameHasFreeRoadPlacements(map))
    {
        return evaluate_player_position(map, player, difficulty);
    }

    skippedMap = *map;
    skippedMap.freeRoadPlacementsRemaining = 0;
    bestScore = search_turn_score(&skippedMap, player, difficulty, state, NULL);

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            struct Map simulatedMap;
            float score;

            if (!IsCanonicalSharedEdge(tileId, sideIndex) ||
                IsSharedEdgeOccupied(map, tileId, sideIndex) ||
                !boardIsValidRoadPlacement(map, tileId, sideIndex, player, origin, BOARD_HEX_RADIUS))
            {
                continue;
            }

            simulatedMap = *map;
            PlaceRoadOnSharedEdge(&simulatedMap, tileId, sideIndex, player);
            gameRefreshAwards(&simulatedMap);
            gameConsumeFreeRoadPlacement(&simulatedMap);
            gameCheckVictory(&simulatedMap, player);
            score = search_turn_score(&simulatedMap, player, difficulty, state, NULL);
            if (!found || score > bestScore + epsilon)
            {
                found = true;
                bestScore = score;
                if (bestAction != NULL)
                {
                    bestAction->type = AI_ACTION_BUILD_ROAD;
                    bestAction->tileId = tileId;
                    bestAction->sideIndex = sideIndex;
                }
            }
        }
    }

    return bestScore;
}

static float search_turn_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiSearchState state, struct AiAction *bestAction)
{
    const float epsilon = 0.001f;
    Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    float bestScore;
    bool foundAction = false;

    if (bestAction != NULL)
    {
        memset(bestAction, 0, sizeof(*bestAction));
        bestAction->type = AI_ACTION_NONE;
    }

    if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return -AI_SEARCH_WIN_SCORE;
    }

    if (gameHasWinner(map))
    {
        return gameGetWinner(map) == player ? AI_SEARCH_WIN_SCORE : -AI_SEARCH_WIN_SCORE;
    }

    if (gameNeedsThiefPlacement(map))
    {
        return search_thief_move_score(map, player, difficulty, state, bestAction);
    }
    if (gameNeedsThiefVictimSelection(map))
    {
        return search_thief_victim_score(map, player, difficulty, state, bestAction);
    }
    if (gameHasFreeRoadPlacements(map))
    {
        return search_free_road_score(map, player, difficulty, state, bestAction);
    }

    bestScore = evaluate_player_position(map, player, difficulty);

    if (gameCanPlayDevelopmentCard(map, DEVELOPMENT_CARD_KNIGHT))
    {
        struct Map simulatedMap = *map;
        if (gameTryPlayKnight(&simulatedMap))
        {
            float score;
            gameCheckVictory(&simulatedMap, player);
            score = search_turn_score(&simulatedMap, player, difficulty, state, NULL);
            if (score > bestScore + epsilon)
            {
                bestScore = score;
                foundAction = true;
                if (bestAction != NULL)
                {
                    bestAction->type = AI_ACTION_PLAY_KNIGHT;
                }
            }
        }
    }

    if (gameCanPlayDevelopmentCard(map, DEVELOPMENT_CARD_ROAD_BUILDING))
    {
        struct Map simulatedMap = *map;
        if (gameTryPlayRoadBuilding(&simulatedMap))
        {
            const float score = search_turn_score(&simulatedMap, player, difficulty, state, NULL);
            if (score > bestScore + epsilon)
            {
                bestScore = score;
                foundAction = true;
                if (bestAction != NULL)
                {
                    bestAction->type = AI_ACTION_PLAY_ROAD_BUILDING;
                }
            }
        }
    }

    if (gameCanPlayDevelopmentCard(map, DEVELOPMENT_CARD_YEAR_OF_PLENTY))
    {
        for (int first = RESOURCE_WOOD; first <= RESOURCE_STONE; first++)
        {
            for (int second = RESOURCE_WOOD; second <= RESOURCE_STONE; second++)
            {
                struct Map simulatedMap = *map;
                if (!gameTryPlayYearOfPlenty(&simulatedMap, (enum ResourceType)first, (enum ResourceType)second))
                {
                    continue;
                }

                {
                    const float score = search_turn_score(&simulatedMap, player, difficulty, state, NULL);
                    if (score > bestScore + epsilon)
                    {
                        bestScore = score;
                        foundAction = true;
                        if (bestAction != NULL)
                        {
                            bestAction->type = AI_ACTION_PLAY_YEAR_OF_PLENTY;
                            bestAction->resourceA = (enum ResourceType)first;
                            bestAction->resourceB = (enum ResourceType)second;
                        }
                    }
                }
            }
        }
    }

    if (gameCanPlayDevelopmentCard(map, DEVELOPMENT_CARD_MONOPOLY))
    {
        for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
        {
            struct Map simulatedMap = *map;
            if (!gameTryPlayMonopoly(&simulatedMap, (enum ResourceType)resource))
            {
                continue;
            }

            {
                const float score = search_turn_score(&simulatedMap, player, difficulty, state, NULL);
                if (score > bestScore + epsilon)
                {
                    bestScore = score;
                    foundAction = true;
                    if (bestAction != NULL)
                    {
                        bestAction->type = AI_ACTION_PLAY_MONOPOLY;
                        bestAction->resourceA = (enum ResourceType)resource;
                    }
                }
            }
        }
    }

    if (state.maritimeTradesRemaining > 0)
    {
        struct AiSearchState nextState = state;
        nextState.maritimeTradesRemaining--;

        for (int give = RESOURCE_WOOD; give <= RESOURCE_STONE; give++)
        {
            for (int receive = RESOURCE_WOOD; receive <= RESOURCE_STONE; receive++)
            {
                struct Map simulatedMap = *map;
                if (!gameTryTradeMaritime(&simulatedMap, (enum ResourceType)give, 1, (enum ResourceType)receive))
                {
                    continue;
                }

                {
                    const float score = search_turn_score(&simulatedMap, player, difficulty, nextState, NULL);
                    if (score > bestScore + epsilon)
                    {
                        bestScore = score;
                        foundAction = true;
                        if (bestAction != NULL)
                        {
                            bestAction->type = AI_ACTION_MARITIME_TRADE;
                            bestAction->resourceA = (enum ResourceType)give;
                            bestAction->resourceB = (enum ResourceType)receive;
                        }
                    }
                }
            }
        }
    }

    if (state.buildActionsRemaining > 0)
    {
        struct AiSearchState nextState = state;
        nextState.buildActionsRemaining--;

        if (gameCanAffordSettlement(map))
        {
            for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
            {
                for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
                {
                    struct Map simulatedMap;
                    float score;

                    if (!IsCanonicalSharedCorner(tileId, cornerIndex) ||
                        !boardIsValidSettlementPlacement(map, tileId, cornerIndex, player, origin, BOARD_HEX_RADIUS))
                    {
                        continue;
                    }

                    simulatedMap = *map;
                    if (!gameTryBuySettlement(&simulatedMap))
                    {
                        continue;
                    }

                    PlaceSettlementOnSharedCorner(&simulatedMap, tileId, cornerIndex, player, STRUCTURE_TOWN);
                    gameRefreshAwards(&simulatedMap);
                    gameCheckVictory(&simulatedMap, player);
                    score = search_turn_score(&simulatedMap, player, difficulty, nextState, NULL);
                    if (score > bestScore + epsilon)
                    {
                        bestScore = score;
                        foundAction = true;
                        if (bestAction != NULL)
                        {
                            bestAction->type = AI_ACTION_BUILD_SETTLEMENT;
                            bestAction->tileId = tileId;
                            bestAction->cornerIndex = cornerIndex;
                        }
                    }
                }
            }
        }

        if (gameCanAffordCity(map))
        {
            for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
            {
                for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
                {
                    struct Map simulatedMap;
                    float score;

                    if (!IsCanonicalSharedCorner(tileId, cornerIndex) ||
                        !boardIsValidCityPlacement(map, tileId, cornerIndex, player))
                    {
                        continue;
                    }

                    simulatedMap = *map;
                    if (!gameTryBuyCity(&simulatedMap))
                    {
                        continue;
                    }

                    PlaceSettlementOnSharedCorner(&simulatedMap, tileId, cornerIndex, player, STRUCTURE_CITY);
                    gameRefreshAwards(&simulatedMap);
                    gameCheckVictory(&simulatedMap, player);
                    score = search_turn_score(&simulatedMap, player, difficulty, nextState, NULL);
                    if (score > bestScore + epsilon)
                    {
                        bestScore = score;
                        foundAction = true;
                        if (bestAction != NULL)
                        {
                            bestAction->type = AI_ACTION_BUILD_CITY;
                            bestAction->tileId = tileId;
                            bestAction->cornerIndex = cornerIndex;
                        }
                    }
                }
            }
        }

        if (gameCanAffordRoad(map))
        {
            for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
            {
                for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
                {
                    struct Map simulatedMap;
                    float score;

                    if (!IsCanonicalSharedEdge(tileId, sideIndex) ||
                        IsSharedEdgeOccupied(map, tileId, sideIndex) ||
                        !boardIsValidRoadPlacement(map, tileId, sideIndex, player, origin, BOARD_HEX_RADIUS))
                    {
                        continue;
                    }

                    simulatedMap = *map;
                    if (!gameTryBuyRoad(&simulatedMap))
                    {
                        continue;
                    }

                    PlaceRoadOnSharedEdge(&simulatedMap, tileId, sideIndex, player);
                    gameRefreshAwards(&simulatedMap);
                    gameCheckVictory(&simulatedMap, player);
                    score = search_turn_score(&simulatedMap, player, difficulty, nextState, NULL);
                    if (score > bestScore + epsilon)
                    {
                        bestScore = score;
                        foundAction = true;
                        if (bestAction != NULL)
                        {
                            bestAction->type = AI_ACTION_BUILD_ROAD;
                            bestAction->tileId = tileId;
                            bestAction->sideIndex = sideIndex;
                        }
                    }
                }
            }
        }

        if (gameCanBuyDevelopment(map))
        {
            struct Map simulatedMap = *map;
            enum DevelopmentCardType drawnCard = DEVELOPMENT_CARD_KNIGHT;

            if (gameTryBuyDevelopment(&simulatedMap, &drawnCard))
            {
                gameCheckVictory(&simulatedMap, player);
                const float score = search_turn_score(&simulatedMap, player, difficulty, nextState, NULL);
                if (score > bestScore + epsilon)
                {
                    bestScore = score;
                    foundAction = true;
                    if (bestAction != NULL)
                    {
                        bestAction->type = AI_ACTION_BUY_DEVELOPMENT;
                    }
                }
            }
        }
    }

    if (!foundAction && bestAction != NULL)
    {
        bestAction->type = AI_ACTION_NONE;
    }

    return bestScore;
}

static bool find_best_play_phase_action(const struct Map *map, enum AiDifficulty difficulty, struct AiAction *action)
{
    struct AiSearchState state;

    if (map == NULL || action == NULL)
    {
        return false;
    }

    state.buildActionsRemaining = ai_build_action_budget(difficulty) - gAiBuildActionsThisTurn;
    state.maritimeTradesRemaining = ai_maritime_trade_budget(difficulty) - gAiMaritimeTradesThisTurn;
    if (state.buildActionsRemaining < 0)
    {
        state.buildActionsRemaining = 0;
    }
    if (state.maritimeTradesRemaining < 0)
    {
        state.maritimeTradesRemaining = 0;
    }

    search_turn_score(map, map->currentPlayer, difficulty, state, action);
    return action->type != AI_ACTION_NONE;
}

static bool execute_ai_action(struct Map *map, const struct AiAction *action)
{
    Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};

    if (map == NULL || action == NULL)
    {
        return false;
    }

    switch (action->type)
    {
    case AI_ACTION_BUILD_SETTLEMENT:
        if (!IsCanonicalSharedCorner(action->tileId, action->cornerIndex) ||
            !boardIsValidSettlementPlacement(map, action->tileId, action->cornerIndex, map->currentPlayer, origin, BOARD_HEX_RADIUS) ||
            !gameTryBuySettlement(map))
        {
            return false;
        }
        PlaceSettlementOnSharedCorner(map, action->tileId, action->cornerIndex, map->currentPlayer, STRUCTURE_TOWN);
        gameRefreshAwards(map);
        finalize_ai_victory(map);
        return true;

    case AI_ACTION_BUILD_CITY:
        if (!IsCanonicalSharedCorner(action->tileId, action->cornerIndex) ||
            !boardIsValidCityPlacement(map, action->tileId, action->cornerIndex, map->currentPlayer) ||
            !gameTryBuyCity(map))
        {
            return false;
        }
        PlaceSettlementOnSharedCorner(map, action->tileId, action->cornerIndex, map->currentPlayer, STRUCTURE_CITY);
        gameRefreshAwards(map);
        finalize_ai_victory(map);
        return true;

    case AI_ACTION_BUILD_ROAD:
        if (!IsCanonicalSharedEdge(action->tileId, action->sideIndex) ||
            IsSharedEdgeOccupied(map, action->tileId, action->sideIndex) ||
            !boardIsValidRoadPlacement(map, action->tileId, action->sideIndex, map->currentPlayer, origin, BOARD_HEX_RADIUS))
        {
            return false;
        }

        if (!gameHasFreeRoadPlacements(map) && !gameTryBuyRoad(map))
        {
            return false;
        }

        PlaceRoadOnSharedEdge(map, action->tileId, action->sideIndex, map->currentPlayer);
        gameRefreshAwards(map);
        if (gameHasFreeRoadPlacements(map))
        {
            gameConsumeFreeRoadPlacement(map);
        }
        finalize_ai_victory(map);
        return true;

    case AI_ACTION_BUY_DEVELOPMENT:
    {
        enum DevelopmentCardType drawnCard = DEVELOPMENT_CARD_KNIGHT;
        if (!gameTryBuyDevelopment(map, &drawnCard))
        {
            return false;
        }
        if (uiGetAiSpeedSetting() < 10)
        {
            uiStartDevelopmentCardDrawAnimation(drawnCard);
        }
        finalize_ai_victory(map);
        return true;
    }

    case AI_ACTION_PLAY_KNIGHT:
        if (!gameTryPlayKnight(map))
        {
            return false;
        }
        finalize_ai_victory(map);
        return true;

    case AI_ACTION_PLAY_ROAD_BUILDING:
        return gameTryPlayRoadBuilding(map);

    case AI_ACTION_PLAY_YEAR_OF_PLENTY:
        return gameTryPlayYearOfPlenty(map, action->resourceA, action->resourceB);

    case AI_ACTION_PLAY_MONOPOLY:
        return gameTryPlayMonopoly(map, action->resourceA);

    case AI_ACTION_MARITIME_TRADE:
        return gameTryTradeMaritime(map, action->resourceA, 1, action->resourceB);

    case AI_ACTION_MOVE_THIEF:
        if (!gameCanMoveThiefToTile(map, action->tileId))
        {
            return false;
        }
        gameMoveThief(map, action->tileId);
        return true;

    case AI_ACTION_CHOOSE_THIEF_VICTIM:
        return gameStealRandomResource(map, action->player);

    case AI_ACTION_SETUP_SETTLEMENT:
        if (!IsCanonicalSharedCorner(action->tileId, action->cornerIndex) ||
            !boardIsValidSettlementPlacement(map, action->tileId, action->cornerIndex, map->currentPlayer, origin, BOARD_HEX_RADIUS))
        {
            return false;
        }
        PlaceSettlementOnSharedCorner(map, action->tileId, action->cornerIndex, map->currentPlayer, STRUCTURE_TOWN);
        gameRefreshAwards(map);
        gameHandlePlacedSettlement(map, action->tileId, action->cornerIndex);
        return true;

    case AI_ACTION_SETUP_ROAD:
        if (!IsCanonicalSharedEdge(action->tileId, action->sideIndex) ||
            IsSharedEdgeOccupied(map, action->tileId, action->sideIndex) ||
            !boardIsValidRoadPlacement(map, action->tileId, action->sideIndex, map->currentPlayer, origin, BOARD_HEX_RADIUS) ||
            !boardEdgeTouchesCorner(action->tileId, action->sideIndex, map->setupSettlementTileId, map->setupSettlementCornerIndex, origin, BOARD_HEX_RADIUS))
        {
            return false;
        }
        PlaceRoadOnSharedEdge(map, action->tileId, action->sideIndex, map->currentPlayer);
        gameRefreshAwards(map);
        gameHandlePlacedRoad(map);
        return true;

    case AI_ACTION_DISCARD:
        return gameTrySubmitDiscard(map, action->player, action->discardPlan);

    case AI_ACTION_NONE:
    default:
        return false;
    }
}

static void search_best_discard_plan_recursive(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, int resourceIndex, int remaining, int discardPlan[5], float *bestScore, int bestPlan[5])
{
    if (map == NULL || bestScore == NULL || bestPlan == NULL)
    {
        return;
    }

    if (resourceIndex >= RESOURCE_STONE)
    {
        struct Map simulatedMap;
        struct AiSearchState state;
        float score;

        if (remaining > map->players[player].resources[RESOURCE_STONE])
        {
            return;
        }

        discardPlan[RESOURCE_STONE] = remaining;
        simulatedMap = *map;
        if (!gameTrySubmitDiscard(&simulatedMap, player, discardPlan))
        {
            return;
        }

        if (simulatedMap.currentPlayer == player && !gameHasPendingDiscards(&simulatedMap))
        {
            state.buildActionsRemaining = max_int(ai_build_action_budget(difficulty) - gAiBuildActionsThisTurn, 0);
            state.maritimeTradesRemaining = max_int(ai_maritime_trade_budget(difficulty) - gAiMaritimeTradesThisTurn, 0);
            score = search_turn_score(&simulatedMap, player, difficulty, state, NULL);
        }
        else
        {
            score = evaluate_player_position(&simulatedMap, player, difficulty);
        }

        if (score > *bestScore)
        {
            *bestScore = score;
            memcpy(bestPlan, discardPlan, sizeof(int) * 5);
        }
        return;
    }

    for (int count = 0; count <= remaining && count <= map->players[player].resources[resourceIndex]; count++)
    {
        discardPlan[resourceIndex] = count;
        search_best_discard_plan_recursive(map, player, difficulty, resourceIndex + 1, remaining - count, discardPlan, bestScore, bestPlan);
    }
}

static bool choose_best_discard_action(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiAction *action)
{
    int discardPlan[5] = {0};
    float bestScore = -AI_SEARCH_WIN_SCORE;

    if (map == NULL || action == NULL || !gameHasPendingDiscards(map))
    {
        return false;
    }

    memset(action, 0, sizeof(*action));
    action->type = AI_ACTION_NONE;
    search_best_discard_plan_recursive(map, player, difficulty, RESOURCE_WOOD, gameGetDiscardAmountForPlayer(map, player), discardPlan, &bestScore, action->discardPlan);
    if (bestScore <= -AI_SEARCH_WIN_SCORE * 0.5f)
    {
        return false;
    }

    action->type = AI_ACTION_DISCARD;
    action->player = player;
    return true;
}

static float search_setup_road_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiAction *bestAction)
{
    const float epsilon = 0.001f;
    Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    float bestScore = -AI_SEARCH_WIN_SCORE;
    bool found = false;

    if (bestAction != NULL)
    {
        memset(bestAction, 0, sizeof(*bestAction));
        bestAction->type = AI_ACTION_NONE;
    }

    if (map == NULL || !gameIsSetupRoadTurn(map))
    {
        return evaluate_player_position(map, player, difficulty);
    }

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            struct Map simulatedMap;
            float score;

            if (!IsCanonicalSharedEdge(tileId, sideIndex) ||
                IsSharedEdgeOccupied(map, tileId, sideIndex) ||
                !boardIsValidRoadPlacement(map, tileId, sideIndex, player, origin, BOARD_HEX_RADIUS) ||
                !boardEdgeTouchesCorner(tileId, sideIndex, map->setupSettlementTileId, map->setupSettlementCornerIndex, origin, BOARD_HEX_RADIUS))
            {
                continue;
            }

            simulatedMap = *map;
            PlaceRoadOnSharedEdge(&simulatedMap, tileId, sideIndex, player);
            gameRefreshAwards(&simulatedMap);
            gameHandlePlacedRoad(&simulatedMap);
            score = evaluate_player_position(&simulatedMap, player, difficulty);
            if (!found || score > bestScore + epsilon)
            {
                found = true;
                bestScore = score;
                if (bestAction != NULL)
                {
                    bestAction->type = AI_ACTION_SETUP_ROAD;
                    bestAction->tileId = tileId;
                    bestAction->sideIndex = sideIndex;
                }
            }
        }
    }

    return found ? bestScore : evaluate_player_position(map, player, difficulty);
}

static bool choose_best_setup_settlement_action(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiAction *action)
{
    const float epsilon = 0.001f;
    Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    float bestScore = -AI_SEARCH_WIN_SCORE;
    bool found = false;

    if (map == NULL || action == NULL || !gameIsSetupSettlementTurn(map))
    {
        return false;
    }

    memset(action, 0, sizeof(*action));
    action->type = AI_ACTION_NONE;

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            struct Map simulatedMap;
            float score;

            if (!IsCanonicalSharedCorner(tileId, cornerIndex) ||
                !boardIsValidSettlementPlacement(map, tileId, cornerIndex, player, origin, BOARD_HEX_RADIUS))
            {
                continue;
            }

            simulatedMap = *map;
            PlaceSettlementOnSharedCorner(&simulatedMap, tileId, cornerIndex, player, STRUCTURE_TOWN);
            gameRefreshAwards(&simulatedMap);
            gameHandlePlacedSettlement(&simulatedMap, tileId, cornerIndex);
            score = search_setup_road_score(&simulatedMap, player, difficulty, NULL);
            if (!found || score > bestScore + epsilon)
            {
                found = true;
                bestScore = score;
                action->type = AI_ACTION_SETUP_SETTLEMENT;
                action->tileId = tileId;
                action->cornerIndex = cornerIndex;
            }
        }
    }

    return found;
}

static bool choose_best_setup_road_action(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct AiAction *action)
{
    if (map == NULL || action == NULL || !gameIsSetupRoadTurn(map))
    {
        return false;
    }

    search_setup_road_score(map, player, difficulty, action);
    return action->type == AI_ACTION_SETUP_ROAD;
}

static bool choose_best_thief_move_action(const struct Map *map, enum AiDifficulty difficulty, struct AiAction *action)
{
    struct AiSearchState state;

    if (map == NULL || action == NULL || !gameNeedsThiefPlacement(map))
    {
        return false;
    }

    state.buildActionsRemaining = max_int(ai_build_action_budget(difficulty) - gAiBuildActionsThisTurn, 0);
    state.maritimeTradesRemaining = max_int(ai_maritime_trade_budget(difficulty) - gAiMaritimeTradesThisTurn, 0);
    search_thief_move_score(map, map->currentPlayer, difficulty, state, action);
    return action->type == AI_ACTION_MOVE_THIEF;
}

static bool choose_best_thief_victim_action(const struct Map *map, enum AiDifficulty difficulty, struct AiAction *action)
{
    struct AiSearchState state;

    if (map == NULL || action == NULL || !gameNeedsThiefVictimSelection(map))
    {
        return false;
    }

    state.buildActionsRemaining = max_int(ai_build_action_budget(difficulty) - gAiBuildActionsThisTurn, 0);
    state.maritimeTradesRemaining = max_int(ai_maritime_trade_budget(difficulty) - gAiMaritimeTradesThisTurn, 0);
    search_thief_victim_score(map, map->currentPlayer, difficulty, state, action);
    return action->type == AI_ACTION_CHOOSE_THIEF_VICTIM;
}

static bool choose_best_free_road_action(const struct Map *map, enum AiDifficulty difficulty, struct AiAction *action)
{
    struct AiSearchState state;

    if (map == NULL || action == NULL || !gameHasFreeRoadPlacements(map))
    {
        return false;
    }

    state.buildActionsRemaining = max_int(ai_build_action_budget(difficulty) - gAiBuildActionsThisTurn, 0);
    state.maritimeTradesRemaining = max_int(ai_maritime_trade_budget(difficulty) - gAiMaritimeTradesThisTurn, 0);
    search_free_road_score(map, map->currentPlayer, difficulty, state, action);
    return action->type == AI_ACTION_BUILD_ROAD;
}
