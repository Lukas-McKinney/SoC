#include "ai_controller.h"

#include "board_rules.h"
#include "game_logic.h"
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

enum AiBuildActionType
{
    AI_BUILD_ACTION_NONE,
    AI_BUILD_ACTION_SETTLEMENT,
    AI_BUILD_ACTION_CITY,
    AI_BUILD_ACTION_ROAD,
    AI_BUILD_ACTION_DEVELOPMENT
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
static float random_jitter(enum AiDifficulty difficulty, float amplitude);
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

static bool handle_ai_discard(struct Map *map, enum PlayerType player, enum AiDifficulty difficulty);
static bool handle_ai_thief_victim(struct Map *map, enum AiDifficulty difficulty);
static bool handle_ai_thief_move(struct Map *map, enum AiDifficulty difficulty);
static bool handle_ai_setup_settlement(struct Map *map, enum AiDifficulty difficulty);
static bool handle_ai_setup_road(struct Map *map, enum AiDifficulty difficulty);
static bool handle_ai_roll(struct Map *map);
static bool try_ai_development_play(struct Map *map, enum AiDifficulty difficulty);
static bool try_ai_maritime_trade(struct Map *map, enum AiDifficulty difficulty);
static float evaluate_settlement_action_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct CornerCandidate *candidate);
static float evaluate_city_action_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct CornerCandidate *candidate);
static float evaluate_road_action_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct EdgeCandidate *candidate);
static float evaluate_development_buy_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty);
static bool try_ai_build_action(struct Map *map, enum AiDifficulty difficulty);
static bool try_ai_build_settlement(struct Map *map, enum AiDifficulty difficulty);
static bool try_ai_build_city(struct Map *map, enum AiDifficulty difficulty);
static bool try_ai_build_road(struct Map *map, enum AiDifficulty difficulty, bool freeRoad);
static bool try_ai_buy_development(struct Map *map, enum AiDifficulty difficulty);
static bool try_ai_play_knight(struct Map *map, enum AiDifficulty difficulty);
static bool try_ai_play_road_building(struct Map *map, enum AiDifficulty difficulty);
static bool try_ai_play_year_of_plenty(struct Map *map, enum AiDifficulty difficulty);
static bool try_ai_play_monopoly(struct Map *map, enum AiDifficulty difficulty);

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
    switch (difficulty)
    {
    case AI_DIFFICULTY_EASY:
        return "Easy";
    case AI_DIFFICULTY_MEDIUM:
        return "Medium";
    case AI_DIFFICULTY_HARD:
        return "Hard";
    default:
        return "Easy";
    }
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
        if (try_ai_build_road(map, difficulty, true))
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

    if (try_ai_development_play(map, difficulty))
    {
        schedule_next_ai_action(difficulty);
        return;
    }

    if (gAiBuildActionsThisTurn < ai_build_action_budget(difficulty) &&
        try_ai_maritime_trade(map, difficulty))
    {
        gAiMaritimeTradesThisTurn++;
        schedule_next_ai_action(difficulty);
        return;
    }

    if (gAiBuildActionsThisTurn < ai_build_action_budget(difficulty) &&
        try_ai_build_action(map, difficulty))
    {
        gAiBuildActionsThisTurn++;
        schedule_next_ai_action(difficulty);
        return;
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
    int simulatedResources[5];
    float beforeScore;
    float afterScore;
    float fairnessScore;
    float threshold;
    const enum AiDifficulty difficulty = active_ai_difficulty(map, aiPlayer);
    const int aiVisiblePoints = gameComputeVisibleVictoryPoints(map, aiPlayer);
    const int offeringPlayerPoints = gameComputeVisibleVictoryPoints(map, map != NULL ? map->currentPlayer : PLAYER_NONE);

    if (map == NULL ||
        aiPlayer < PLAYER_RED || aiPlayer > PLAYER_BLACK ||
        !player_is_ai(map, aiPlayer) ||
        !gameCanTradeWithPlayer(map, aiPlayer, give, giveAmount, receive, receiveAmount))
    {
        return false;
    }

    memcpy(simulatedResources, map->players[aiPlayer].resources, sizeof(simulatedResources));
    simulatedResources[receive] -= receiveAmount;
    simulatedResources[give] += giveAmount;

    beforeScore = evaluate_hand_value(map, aiPlayer, map->players[aiPlayer].resources, difficulty);
    afterScore = evaluate_hand_value(map, aiPlayer, simulatedResources, difficulty);
    fairnessScore = (float)giveAmount * resource_weight(give, difficulty) -
                    (float)receiveAmount * resource_weight(receive, difficulty);

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

    return (afterScore - beforeScore) + fairnessScore >= threshold + random_jitter(difficulty, 0.4f);
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

static float random_jitter(enum AiDifficulty difficulty, float amplitude)
{
    float scale = 1.0f;
    if (difficulty == AI_DIFFICULTY_MEDIUM)
    {
        scale = 0.35f;
    }
    else if (difficulty == AI_DIFFICULTY_HARD)
    {
        scale = 0.12f;
    }

    return ((((float)rand() / (float)RAND_MAX) * 2.0f) - 1.0f) * amplitude * scale;
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

            score = evaluate_corner_value(map, tileId, cornerIndex, difficulty) + random_jitter(difficulty, 3.2f);
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

            score = evaluate_corner_value(map, tileId, cornerIndex, difficulty) * 1.35f + random_jitter(difficulty, 2.0f);
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

            score = evaluate_road_candidate(map, tileId, sideIndex, player, difficulty) + random_jitter(difficulty, 2.8f);
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
    int discardPlan[5] = {0};
    int remaining;

    if (map == NULL || !gameHasPendingDiscards(map))
    {
        return false;
    }

    remaining = gameGetDiscardAmountForPlayer(map, player);
    while (remaining > 0)
    {
        int bestResource = -1;
        float bestScore = -FLT_MAX;

        for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
        {
            int available = map->players[player].resources[resource] - discardPlan[resource];
            float keepBias = difficulty == AI_DIFFICULTY_HARD ? resource_weight((enum ResourceType)resource, difficulty) : 1.0f;
            float score;

            if (available <= 0)
            {
                continue;
            }

            score = (float)available * (difficulty == AI_DIFFICULTY_EASY ? 1.0f : 1.35f) - keepBias * 2.2f + random_jitter(difficulty, 0.6f);
            if (bestResource < 0 || score > bestScore)
            {
                bestResource = resource;
                bestScore = score;
            }
        }

        if (bestResource < 0)
        {
            break;
        }

        discardPlan[bestResource]++;
        remaining--;
    }

    return gameTrySubmitDiscard(map, player, discardPlan);
}

static bool handle_ai_thief_victim(struct Map *map, enum AiDifficulty difficulty)
{
    enum PlayerType bestVictim = PLAYER_NONE;
    int bestResources = -1;
    (void)difficulty;

    if (map == NULL || !gameNeedsThiefVictimSelection(map))
    {
        return false;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        enum PlayerType victim = (enum PlayerType)player;
        int resources;
        if (!gameCanStealFromPlayer(map, victim))
        {
            continue;
        }

        resources = resource_total_for_player(map, victim);
        if (bestVictim == PLAYER_NONE || resources > bestResources)
        {
            bestVictim = victim;
            bestResources = resources;
        }
    }

    return bestVictim != PLAYER_NONE && gameStealRandomResource(map, bestVictim);
}

static bool handle_ai_thief_move(struct Map *map, enum AiDifficulty difficulty)
{
    int bestTile = -1;
    float bestScore = -FLT_MAX;
    bool foundNonDesert = false;

    if (map == NULL || !gameNeedsThiefPlacement(map))
    {
        return false;
    }

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        float score = 0.0f;
        if (tileId == map->thiefTileId)
        {
            continue;
        }

        if (map->tiles[tileId].type != TILE_DESERT)
        {
            foundNonDesert = true;
        }

        score -= map->tiles[tileId].type == TILE_DESERT ? 6.0f : 0.0f;
        score += (float)dice_weight(map->tiles[tileId].diceNumber) * 0.5f;

        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            const struct Corner *corner = &map->tiles[tileId].corners[cornerIndex];
            float yield;
            if (corner->structure == STRUCTURE_NONE || corner->owner == PLAYER_NONE)
            {
                continue;
            }

            yield = corner->structure == STRUCTURE_CITY ? 2.0f : 1.0f;
            if (corner->owner == map->currentPlayer)
            {
                score -= yield * 3.0f;
            }
            else
            {
                score += yield * (2.2f + 0.4f * (float)resource_total_for_player(map, corner->owner));
            }
        }

        score += random_jitter(difficulty, 1.4f);
        if ((foundNonDesert && map->tiles[tileId].type == TILE_DESERT))
        {
            continue;
        }
        if (bestTile < 0 || score > bestScore)
        {
            bestTile = tileId;
            bestScore = score;
        }
    }

    if (bestTile < 0)
    {
        return false;
    }

    gameMoveThief(map, bestTile);
    return true;
}

static bool handle_ai_setup_settlement(struct Map *map, enum AiDifficulty difficulty)
{
    struct CornerCandidate candidate;
    if (map == NULL || !gameIsSetupSettlementTurn(map) ||
        !find_best_settlement_candidate(map, map->currentPlayer, difficulty, &candidate))
    {
        return false;
    }

    PlaceSettlementOnSharedCorner(map, candidate.tileId, candidate.cornerIndex, map->currentPlayer, STRUCTURE_TOWN);
    gameRefreshAwards(map);
    gameHandlePlacedSettlement(map, candidate.tileId, candidate.cornerIndex);
    return true;
}

static bool handle_ai_setup_road(struct Map *map, enum AiDifficulty difficulty)
{
    struct EdgeCandidate candidate;
    if (map == NULL || !gameIsSetupRoadTurn(map) ||
        !find_best_road_candidate(map, map->currentPlayer, difficulty, true, &candidate))
    {
        return false;
    }

    PlaceRoadOnSharedEdge(map, candidate.tileId, candidate.sideIndex, map->currentPlayer);
    gameRefreshAwards(map);
    gameHandlePlacedRoad(map);
    return true;
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

static bool try_ai_development_play(struct Map *map, enum AiDifficulty difficulty)
{
    if (difficulty == AI_DIFFICULTY_HARD)
    {
        if (try_ai_play_year_of_plenty(map, difficulty) ||
            try_ai_play_monopoly(map, difficulty) ||
            try_ai_play_road_building(map, difficulty) ||
            try_ai_play_knight(map, difficulty))
        {
            return true;
        }
        return false;
    }

    if (difficulty == AI_DIFFICULTY_MEDIUM)
    {
        return try_ai_play_road_building(map, difficulty) ||
               try_ai_play_knight(map, difficulty);
    }

    return false;
}

static bool try_ai_maritime_trade(struct Map *map, enum AiDifficulty difficulty)
{
    const int *targets[] = {kCityCost, kSettlementCost, kRoadCost, kDevelopmentCost};
    int giveResource = -1;
    int receiveResource = -1;
    float bestScore = -FLT_MAX;
    const int maxTrades = difficulty == AI_DIFFICULTY_HARD ? 2 : 1;

    if (map == NULL || difficulty == AI_DIFFICULTY_EASY || gAiMaritimeTradesThisTurn >= maxTrades)
    {
        return false;
    }

    for (int targetIndex = 0; targetIndex < 4; targetIndex++)
    {
        const int *cost = targets[targetIndex];
        int missingResources = 0;
        int missingResourceType = -1;
        bool targetAvailable = true;

        if (targetIndex == 0)
        {
            struct CornerCandidate cityCandidate;
            targetAvailable = find_best_city_candidate(map, map->currentPlayer, difficulty, &cityCandidate);
        }
        else if (targetIndex == 1)
        {
            struct CornerCandidate settlementCandidate;
            targetAvailable = find_best_settlement_candidate(map, map->currentPlayer, difficulty, &settlementCandidate);
        }
        else if (targetIndex == 2)
        {
            struct EdgeCandidate roadCandidate;
            targetAvailable = find_best_road_candidate(map, map->currentPlayer, difficulty, false, &roadCandidate);
        }
        else
        {
            targetAvailable = gameCanBuyDevelopment(map);
        }

        if (!targetAvailable)
        {
            continue;
        }

        for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
        {
            int missing = cost[resource] - map->players[map->currentPlayer].resources[resource];
            if (missing > 0)
            {
                missingResources += missing;
                missingResourceType = resource;
            }
        }

        if (missingResources != 1 || missingResourceType < 0)
        {
            continue;
        }

        for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
        {
            int rate;
            int reserve;
            float score;
            if (resource == missingResourceType)
            {
                continue;
            }

            rate = gameGetMaritimeTradeRate(map, (enum ResourceType)resource);
            reserve = difficulty == AI_DIFFICULTY_HARD ? (resource == RESOURCE_WHEAT || resource == RESOURCE_STONE ? 1 : 0) : 0;
            if (!gameCanTradeMaritime(map, (enum ResourceType)resource, 1, (enum ResourceType)missingResourceType) ||
                map->players[map->currentPlayer].resources[resource] - rate < reserve)
            {
                continue;
            }

            score = (float)(targetIndex == 0 ? 8 : targetIndex == 1 ? 7 : targetIndex == 2 ? 4 : 3) +
                    (float)(map->players[map->currentPlayer].resources[resource] - rate) * 0.2f;
            if (score > bestScore)
            {
                bestScore = score;
                giveResource = resource;
                receiveResource = missingResourceType;
            }
        }
    }

    if (giveResource < 0 || receiveResource < 0)
    {
        return false;
    }

    return gameTryTradeMaritime(map, (enum ResourceType)giveResource, 1, (enum ResourceType)receiveResource);
}

static float evaluate_settlement_action_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct CornerCandidate *candidate)
{
    if (map == NULL || candidate == NULL ||
        !gameCanAffordSettlement(map) ||
        !find_best_settlement_candidate(map, player, difficulty, candidate))
    {
        return -FLT_MAX;
    }

    return 6.1f + candidate->score;
}

static float evaluate_city_action_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct CornerCandidate *candidate)
{
    float score;

    if (map == NULL || candidate == NULL ||
        !gameCanAffordCity(map) ||
        !find_best_city_candidate(map, player, difficulty, candidate))
    {
        return -FLT_MAX;
    }

    score = 6.5f + candidate->score / 1.35f;
    if (gameComputeVictoryPoints(map, player) >= 8)
    {
        score += 2.4f;
    }
    return score;
}

static float evaluate_road_action_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty, struct EdgeCandidate *candidate)
{
    float score;

    if (map == NULL || candidate == NULL ||
        !gameCanAffordRoad(map) ||
        !find_best_road_candidate(map, player, difficulty, false, candidate))
    {
        return -FLT_MAX;
    }

    score = 2.6f + candidate->score * 0.42f;
    if (gameGetLongestRoadOwner(map) != player && gameGetLongestRoadLength(map) >= 4)
    {
        score += 1.8f;
    }
    return score;
}

static float evaluate_development_buy_score(const struct Map *map, enum PlayerType player, enum AiDifficulty difficulty)
{
    float score = 4.4f;

    if (map == NULL || !gameCanBuyDevelopment(map))
    {
        return -FLT_MAX;
    }

    score += difficulty == AI_DIFFICULTY_HARD ? 1.1f : 0.5f;
    score += (float)gameGetDevelopmentDeckCount(map) * 0.04f;
    if (gameGetLargestArmyOwner(map) != player)
    {
        score += 0.75f;
    }
    if (gameComputeVictoryPoints(map, player) >= 7)
    {
        score += 1.25f;
    }
    return score;
}

static bool try_ai_build_action(struct Map *map, enum AiDifficulty difficulty)
{
    struct CornerCandidate settlementCandidate;
    struct CornerCandidate cityCandidate;
    struct EdgeCandidate roadCandidate;
    float settlementScore;
    float cityScore;
    float roadScore;
    float developmentScore;

    if (difficulty == AI_DIFFICULTY_EASY)
    {
        int choices[4] = {0, 1, 2, 3};

        for (int i = 3; i > 0; i--)
        {
            const int swapIndex = GetRandomValue(0, i);
            const int temp = choices[i];
            choices[i] = choices[swapIndex];
            choices[swapIndex] = temp;
        }

        for (int i = 0; i < 4; i++)
        {
            const int choice = choices[i];
            if ((choice == 0 && try_ai_build_settlement(map, difficulty)) ||
                (choice == 1 && try_ai_build_road(map, difficulty, false)) ||
                (choice == 2 && try_ai_build_city(map, difficulty)) ||
                (choice == 3 && try_ai_buy_development(map, difficulty)))
            {
                return true;
            }
        }

        return false;
    }

    settlementScore = evaluate_settlement_action_score(map, map->currentPlayer, difficulty, &settlementCandidate);
    cityScore = evaluate_city_action_score(map, map->currentPlayer, difficulty, &cityCandidate);
    roadScore = evaluate_road_action_score(map, map->currentPlayer, difficulty, &roadCandidate);
    developmentScore = evaluate_development_buy_score(map, map->currentPlayer, difficulty);

    if (difficulty == AI_DIFFICULTY_MEDIUM)
    {
        if (cityScore > -FLT_MAX &&
            (cityScore >= settlementScore + 1.5f || gameComputeVictoryPoints(map, map->currentPlayer) >= 8))
        {
            return try_ai_build_city(map, difficulty);
        }
        if (settlementScore > -FLT_MAX)
        {
            return try_ai_build_settlement(map, difficulty);
        }
        if (roadScore > -FLT_MAX && roadScore >= developmentScore + 0.45f)
        {
            return try_ai_build_road(map, difficulty, false);
        }
        if (developmentScore > -FLT_MAX)
        {
            return try_ai_buy_development(map, difficulty);
        }
        if (cityScore > -FLT_MAX)
        {
            return try_ai_build_city(map, difficulty);
        }
        return false;
    }

    if (difficulty == AI_DIFFICULTY_HARD)
    {
        enum AiBuildActionType bestAction = AI_BUILD_ACTION_NONE;
        float bestScore = -FLT_MAX;

        if (cityScore > bestScore)
        {
            bestScore = cityScore;
            bestAction = AI_BUILD_ACTION_CITY;
        }
        if (settlementScore > bestScore)
        {
            bestScore = settlementScore;
            bestAction = AI_BUILD_ACTION_SETTLEMENT;
        }
        if (roadScore > bestScore)
        {
            bestScore = roadScore;
            bestAction = AI_BUILD_ACTION_ROAD;
        }
        if (developmentScore > bestScore)
        {
            bestScore = developmentScore;
            bestAction = AI_BUILD_ACTION_DEVELOPMENT;
        }

        switch (bestAction)
        {
        case AI_BUILD_ACTION_CITY:
            return try_ai_build_city(map, difficulty);
        case AI_BUILD_ACTION_SETTLEMENT:
            return try_ai_build_settlement(map, difficulty);
        case AI_BUILD_ACTION_ROAD:
            return try_ai_build_road(map, difficulty, false);
        case AI_BUILD_ACTION_DEVELOPMENT:
            return try_ai_buy_development(map, difficulty);
        case AI_BUILD_ACTION_NONE:
        default:
            return false;
        }
    }

    return false;
}

static bool try_ai_build_settlement(struct Map *map, enum AiDifficulty difficulty)
{
    struct CornerCandidate candidate;
    if (map == NULL || !gameCanAffordSettlement(map) ||
        !find_best_settlement_candidate(map, map->currentPlayer, difficulty, &candidate) ||
        !gameTryBuySettlement(map))
    {
        return false;
    }

    PlaceSettlementOnSharedCorner(map, candidate.tileId, candidate.cornerIndex, map->currentPlayer, STRUCTURE_TOWN);
    gameRefreshAwards(map);
    finalize_ai_victory(map);
    return true;
}

static bool try_ai_build_city(struct Map *map, enum AiDifficulty difficulty)
{
    struct CornerCandidate candidate;
    if (map == NULL || !gameCanAffordCity(map) ||
        !find_best_city_candidate(map, map->currentPlayer, difficulty, &candidate) ||
        !gameTryBuyCity(map))
    {
        return false;
    }

    PlaceSettlementOnSharedCorner(map, candidate.tileId, candidate.cornerIndex, map->currentPlayer, STRUCTURE_CITY);
    gameRefreshAwards(map);
    finalize_ai_victory(map);
    return true;
}

static bool try_ai_build_road(struct Map *map, enum AiDifficulty difficulty, bool freeRoad)
{
    struct EdgeCandidate candidate;
    bool hasMoreRoads;

    if (map == NULL ||
        (!freeRoad && !gameCanAffordRoad(map)) ||
        !find_best_road_candidate(map, map->currentPlayer, difficulty, false, &candidate))
    {
        return false;
    }

    if (!freeRoad && !gameTryBuyRoad(map))
    {
        return false;
    }

    PlaceRoadOnSharedEdge(map, candidate.tileId, candidate.sideIndex, map->currentPlayer);
    gameRefreshAwards(map);
    if (freeRoad)
    {
        gameConsumeFreeRoadPlacement(map);
        hasMoreRoads = find_best_road_candidate(map, map->currentPlayer, difficulty, false, &candidate);
        if (!hasMoreRoads)
        {
            map->freeRoadPlacementsRemaining = 0;
        }
    }
    finalize_ai_victory(map);
    return true;
}

static bool try_ai_buy_development(struct Map *map, enum AiDifficulty difficulty)
{
    enum DevelopmentCardType drawnCard;
    (void)difficulty;

    if (map == NULL || !gameCanBuyDevelopment(map))
    {
        return false;
    }

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

static bool try_ai_play_knight(struct Map *map, enum AiDifficulty difficulty)
{
    int bestTile = -1;
    float bestScore = -FLT_MAX;

    if (map == NULL || !gameCanPlayDevelopmentCard(map, DEVELOPMENT_CARD_KNIGHT))
    {
        return false;
    }

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        float score = 0.0f;
        if (!gameCanMoveThiefToTile(map, tileId))
        {
            continue;
        }

        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            const struct Corner *corner = &map->tiles[tileId].corners[cornerIndex];
            if (corner->structure == STRUCTURE_NONE || corner->owner == PLAYER_NONE)
            {
                continue;
            }
            score += corner->owner == map->currentPlayer ? -3.0f : 3.5f;
        }

        score += random_jitter(difficulty, 0.8f);
        if (bestTile < 0 || score > bestScore)
        {
            bestTile = tileId;
            bestScore = score;
        }
    }

    if (bestTile < 0)
    {
        return false;
    }

    if (!gameTryPlayKnight(map))
    {
        return false;
    }

    finalize_ai_victory(map);
    return true;
}

static bool try_ai_play_road_building(struct Map *map, enum AiDifficulty difficulty)
{
    struct EdgeCandidate candidate;
    if (map == NULL ||
        !gameCanPlayDevelopmentCard(map, DEVELOPMENT_CARD_ROAD_BUILDING) ||
        !find_best_road_candidate(map, map->currentPlayer, difficulty, false, &candidate))
    {
        return false;
    }

    return gameTryPlayRoadBuilding(map);
}

static bool try_ai_play_year_of_plenty(struct Map *map, enum AiDifficulty difficulty)
{
    static const int kCosts[][5] = {
        {0, 2, 0, 0, 3},
        {1, 1, 1, 1, 0},
        {1, 0, 1, 0, 0},
        {0, 1, 0, 1, 1}};
    enum ResourceType first = RESOURCE_WOOD;
    enum ResourceType second = RESOURCE_WOOD;
    float bestScore = -FLT_MAX;

    if (map == NULL || difficulty != AI_DIFFICULTY_HARD ||
        !gameCanPlayDevelopmentCard(map, DEVELOPMENT_CARD_YEAR_OF_PLENTY))
    {
        return false;
    }

    for (int costIndex = 0; costIndex < 4; costIndex++)
    {
        for (int firstResource = RESOURCE_WOOD; firstResource <= RESOURCE_STONE; firstResource++)
        {
            for (int secondResource = RESOURCE_WOOD; secondResource <= RESOURCE_STONE; secondResource++)
            {
                int simulated[5];
                int covered = 0;
                float score;
                memcpy(simulated, map->players[map->currentPlayer].resources, sizeof(simulated));
                simulated[firstResource]++;
                simulated[secondResource]++;

                for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
                {
                    covered += simulated[resource] >= kCosts[costIndex][resource] ? kCosts[costIndex][resource] : simulated[resource];
                }

                score = (float)covered + (float)(costIndex == 0 ? 2.5f : costIndex == 1 ? 2.0f : 1.0f);
                if (score > bestScore)
                {
                    bestScore = score;
                    first = (enum ResourceType)firstResource;
                    second = (enum ResourceType)secondResource;
                }
            }
        }
    }

    return gameTryPlayYearOfPlenty(map, first, second);
}

static bool try_ai_play_monopoly(struct Map *map, enum AiDifficulty difficulty)
{
    enum ResourceType bestResource = RESOURCE_WOOD;
    int bestTotal = 0;

    if (map == NULL || difficulty != AI_DIFFICULTY_HARD ||
        !gameCanPlayDevelopmentCard(map, DEVELOPMENT_CARD_MONOPOLY))
    {
        return false;
    }

    for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
    {
        int total = 0;
        for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
        {
            if (player == map->currentPlayer)
            {
                continue;
            }
            total += map->players[player].resources[resource];
        }

        if (total > bestTotal)
        {
            bestTotal = total;
            bestResource = (enum ResourceType)resource;
        }
    }

    if (bestTotal < 3)
    {
        return false;
    }

    return gameTryPlayMonopoly(map, bestResource);
}
