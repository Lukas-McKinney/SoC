#include "board_rules.h"
#include "game_logic.h"
#include "map.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LAND_TILE_COUNT 19
#define HEX_CORNERS 6
#define INPUT_LINE_MAX 512

enum BuildType
{
    BUILD_ROAD,
    BUILD_SETTLEMENT,
    BUILD_CITY
};

struct AxialCoord
{
    int q;
    int r;
};

struct ConsoleState
{
    struct Map map;
    int pendingRoad[MAX_PLAYERS];
    int pendingSettlement[MAX_PLAYERS];
    int pendingCity[MAX_PLAYERS];
    bool privateViewEnabled;
    enum PlayerType revealedPlayer;
};

static const struct AxialCoord kLandCoords[LAND_TILE_COUNT] = {
    {0, -2}, {1, -2}, {2, -2}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-2, 0}, {-1, 0}, {0, 0},
    {1, 0}, {2, 0}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}, {-2, 2}, {-1, 2}, {0, 2}};

static const Vector2 kBoardOrigin = {1600.0f * 0.42f, 900.0f * 0.46f};
static const float kBoardRadius = 68.0f;

static void print_help(void);
static void print_status(const struct ConsoleState *state);
static void print_board(const struct ConsoleState *state);
static void print_hands(const struct ConsoleState *state, bool allPlayers);
static void print_bank(const struct ConsoleState *state);
static enum PlayerType shown_player(const struct ConsoleState *state);
static bool parse_view_target(const struct ConsoleState *state, const char *text, enum PlayerType *playerOut);

static const char *player_name(enum PlayerType player);
static const char *phase_name(enum GamePhase phase);
static const char *resource_name(enum ResourceType resource);
static const char *tile_type_name(enum TileType type);
static const char *dev_card_name(enum DevelopmentCardType card);

static bool equals_ignore_case(const char *a, const char *b);
static bool parse_int(const char *text, int *valueOut);
static bool parse_player(const char *text, enum PlayerType *playerOut);
static bool parse_resource(const char *text, enum ResourceType *resourceOut);
static bool parse_build_type(const char *text, enum BuildType *typeOut);
static bool parse_dev_play_type(const char *text, enum DevelopmentCardType *cardOut);
static int roll_two_dice(void);

static Vector2 axial_to_world(struct AxialCoord coord, Vector2 origin, float radius);
static Vector2 point_on_hex(Vector2 center, float radius, int cornerIndex);
static void get_corner_key(Vector2 center, float radius, int cornerIndex, int *x, int *y);
static void get_side_corner_indices(int sideIndex, int *cornerA, int *cornerB);
static void get_edge_key(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by);
static bool corner_keys_match(int x1, int y1, int x2, int y2);
static bool edge_keys_match(int ax1, int ay1, int bx1, int by1, int ax2, int ay2, int bx2, int by2);
static void apply_settlement_on_shared_corner(struct Map *map, int tileId, int cornerIndex, enum PlayerType player, enum StructureType structure);
static void apply_road_on_shared_edge(struct Map *map, int tileId, int sideIndex, enum PlayerType player);

static bool has_pending_builds_for_current_player(const struct ConsoleState *state);
static bool try_place_settlement(struct ConsoleState *state, int tileId, int cornerIndex);
static bool try_place_city(struct ConsoleState *state, int tileId, int cornerIndex);
static bool try_place_road(struct ConsoleState *state, int tileId, int sideIndex);
static bool try_buy_build(struct ConsoleState *state, enum BuildType buildType);

static bool parse_int(const char *text, int *valueOut)
{
    char *end = NULL;
    long value = 0;

    if (text == NULL || valueOut == NULL)
    {
        return false;
    }

    value = strtol(text, &end, 10);
    if (end == text || *end != '\0')
    {
        return false;
    }

    *valueOut = (int)value;
    return true;
}

static bool equals_ignore_case(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    while (*a != '\0' && *b != '\0')
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
        {
            return false;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static const char *player_name(enum PlayerType player)
{
    switch (player)
    {
    case PLAYER_RED:
        return "red";
    case PLAYER_BLUE:
        return "blue";
    case PLAYER_GREEN:
        return "green";
    case PLAYER_BLACK:
        return "black";
    default:
        return "none";
    }
}

static const char *phase_name(enum GamePhase phase)
{
    switch (phase)
    {
    case GAME_PHASE_SETUP:
        return "setup";
    case GAME_PHASE_PLAY:
        return "play";
    case GAME_PHASE_GAME_OVER:
        return "game_over";
    default:
        return "unknown";
    }
}

static const char *resource_name(enum ResourceType resource)
{
    switch (resource)
    {
    case RESOURCE_WOOD:
        return "wood";
    case RESOURCE_WHEAT:
        return "wheat";
    case RESOURCE_CLAY:
        return "clay";
    case RESOURCE_SHEEP:
        return "sheep";
    case RESOURCE_STONE:
        return "stone";
    default:
        return "?";
    }
}

static const char *tile_type_name(enum TileType type)
{
    switch (type)
    {
    case TILE_FARMLAND:
        return "farmland";
    case TILE_SHEEPMEADOW:
        return "sheep";
    case TILE_MINE:
        return "mine";
    case TILE_FOREST:
        return "forest";
    case TILE_MOUNTAIN:
        return "mountain";
    case TILE_DESERT:
        return "desert";
    default:
        return "?";
    }
}

static const char *dev_card_name(enum DevelopmentCardType card)
{
    switch (card)
    {
    case DEVELOPMENT_CARD_KNIGHT:
        return "knight";
    case DEVELOPMENT_CARD_VICTORY_POINT:
        return "victory_point";
    case DEVELOPMENT_CARD_ROAD_BUILDING:
        return "road_building";
    case DEVELOPMENT_CARD_YEAR_OF_PLENTY:
        return "year_of_plenty";
    case DEVELOPMENT_CARD_MONOPOLY:
        return "monopoly";
    default:
        return "unknown";
    }
}

static bool parse_player(const char *text, enum PlayerType *playerOut)
{
    int value = 0;

    if (text == NULL || playerOut == NULL)
    {
        return false;
    }

    if (parse_int(text, &value))
    {
        if (value >= PLAYER_RED && value <= PLAYER_BLACK)
        {
            *playerOut = (enum PlayerType)value;
            return true;
        }
        return false;
    }

    if (equals_ignore_case(text, "red"))
    {
        *playerOut = PLAYER_RED;
        return true;
    }
    if (equals_ignore_case(text, "blue"))
    {
        *playerOut = PLAYER_BLUE;
        return true;
    }
    if (equals_ignore_case(text, "green"))
    {
        *playerOut = PLAYER_GREEN;
        return true;
    }
    if (equals_ignore_case(text, "black"))
    {
        *playerOut = PLAYER_BLACK;
        return true;
    }

    return false;
}

static bool parse_resource(const char *text, enum ResourceType *resourceOut)
{
    if (text == NULL || resourceOut == NULL)
    {
        return false;
    }

    if (equals_ignore_case(text, "wood") || equals_ignore_case(text, "w"))
    {
        *resourceOut = RESOURCE_WOOD;
        return true;
    }
    if (equals_ignore_case(text, "wheat") || equals_ignore_case(text, "wh"))
    {
        *resourceOut = RESOURCE_WHEAT;
        return true;
    }
    if (equals_ignore_case(text, "clay") || equals_ignore_case(text, "brick") || equals_ignore_case(text, "b"))
    {
        *resourceOut = RESOURCE_CLAY;
        return true;
    }
    if (equals_ignore_case(text, "sheep") || equals_ignore_case(text, "s"))
    {
        *resourceOut = RESOURCE_SHEEP;
        return true;
    }
    if (equals_ignore_case(text, "stone") || equals_ignore_case(text, "ore") || equals_ignore_case(text, "o"))
    {
        *resourceOut = RESOURCE_STONE;
        return true;
    }

    return false;
}

static bool parse_build_type(const char *text, enum BuildType *typeOut)
{
    if (text == NULL || typeOut == NULL)
    {
        return false;
    }

    if (equals_ignore_case(text, "road"))
    {
        *typeOut = BUILD_ROAD;
        return true;
    }
    if (equals_ignore_case(text, "settlement") || equals_ignore_case(text, "town"))
    {
        *typeOut = BUILD_SETTLEMENT;
        return true;
    }
    if (equals_ignore_case(text, "city"))
    {
        *typeOut = BUILD_CITY;
        return true;
    }

    return false;
}

static bool parse_dev_play_type(const char *text, enum DevelopmentCardType *cardOut)
{
    if (text == NULL || cardOut == NULL)
    {
        return false;
    }

    if (equals_ignore_case(text, "knight"))
    {
        *cardOut = DEVELOPMENT_CARD_KNIGHT;
        return true;
    }
    if (equals_ignore_case(text, "road_building") || equals_ignore_case(text, "roadbuilding"))
    {
        *cardOut = DEVELOPMENT_CARD_ROAD_BUILDING;
        return true;
    }
    if (equals_ignore_case(text, "year_of_plenty") || equals_ignore_case(text, "yop"))
    {
        *cardOut = DEVELOPMENT_CARD_YEAR_OF_PLENTY;
        return true;
    }
    if (equals_ignore_case(text, "monopoly"))
    {
        *cardOut = DEVELOPMENT_CARD_MONOPOLY;
        return true;
    }

    return false;
}

static enum PlayerType shown_player(const struct ConsoleState *state)
{
    if (state == NULL)
    {
        return PLAYER_NONE;
    }

    if (!state->privateViewEnabled)
    {
        return state->map.currentPlayer;
    }

    if (state->revealedPlayer >= PLAYER_RED && state->revealedPlayer <= PLAYER_BLACK)
    {
        return state->revealedPlayer;
    }

    return state->map.currentPlayer;
}

static bool parse_view_target(const struct ConsoleState *state, const char *text, enum PlayerType *playerOut)
{
    if (text == NULL || playerOut == NULL)
    {
        return false;
    }

    if (equals_ignore_case(text, "current"))
    {
        *playerOut = state->map.currentPlayer;
        return true;
    }

    return parse_player(text, playerOut);
}

static int roll_two_dice(void)
{
    return (rand() % 6 + 1) + (rand() % 6 + 1);
}

static Vector2 axial_to_world(struct AxialCoord coord, Vector2 origin, float radius)
{
    const float root3 = 1.7320508f;
    return (Vector2){
        origin.x + radius * root3 * ((float)coord.q + (float)coord.r * 0.5f),
        origin.y + radius * 1.5f * (float)coord.r};
}

static Vector2 point_on_hex(Vector2 center, float radius, int cornerIndex)
{
    const float angle = DEG2RAD * (60.0f * (float)cornerIndex - 30.0f);
    return (Vector2){
        center.x + cosf(angle) * radius,
        center.y + sinf(angle) * radius};
}

static void get_corner_key(Vector2 center, float radius, int cornerIndex, int *x, int *y)
{
    const Vector2 point = point_on_hex(center, radius, cornerIndex);
    *x = (int)roundf(point.x * 10.0f);
    *y = (int)roundf(point.y * 10.0f);
}

static void get_side_corner_indices(int sideIndex, int *cornerA, int *cornerB)
{
    static const int sideCorners[HEX_CORNERS][2] = {
        {0, 1}, {5, 0}, {4, 5}, {3, 4}, {2, 3}, {1, 2}};
    const int index = ((sideIndex % HEX_CORNERS) + HEX_CORNERS) % HEX_CORNERS;
    *cornerA = sideCorners[index][0];
    *cornerB = sideCorners[index][1];
}

static void get_edge_key(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by)
{
    int cornerA = 0;
    int cornerB = 1;

    get_side_corner_indices(sideIndex, &cornerA, &cornerB);
    get_corner_key(center, radius, cornerA, ax, ay);
    get_corner_key(center, radius, cornerB, bx, by);

    if (*ax > *bx || (*ax == *bx && *ay > *by))
    {
        const int tx = *ax;
        const int ty = *ay;
        *ax = *bx;
        *ay = *by;
        *bx = tx;
        *by = ty;
    }
}

static bool corner_keys_match(int x1, int y1, int x2, int y2)
{
    return x1 == x2 && y1 == y2;
}

static bool edge_keys_match(int ax1, int ay1, int bx1, int by1, int ax2, int ay2, int bx2, int by2)
{
    return ax1 == ax2 && ay1 == ay2 && bx1 == bx2 && by1 == by2;
}

static void apply_settlement_on_shared_corner(struct Map *map, int tileId, int cornerIndex, enum PlayerType player, enum StructureType structure)
{
    const Vector2 center = axial_to_world(kLandCoords[tileId], kBoardOrigin, kBoardRadius);
    int targetX = 0;
    int targetY = 0;

    get_corner_key(center, kBoardRadius, cornerIndex, &targetX, &targetY);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], kBoardOrigin, kBoardRadius);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int x = 0;
            int y = 0;
            get_corner_key(otherCenter, kBoardRadius, otherCorner, &x, &y);
            if (corner_keys_match(targetX, targetY, x, y))
            {
                map->tiles[otherTile].corners[otherCorner].owner = player;
                map->tiles[otherTile].corners[otherCorner].structure = structure;
            }
        }
    }
}

static void apply_road_on_shared_edge(struct Map *map, int tileId, int sideIndex, enum PlayerType player)
{
    const Vector2 center = axial_to_world(kLandCoords[tileId], kBoardOrigin, kBoardRadius);
    int tax = 0;
    int tay = 0;
    int tbx = 0;
    int tby = 0;

    get_edge_key(center, kBoardRadius, sideIndex, &tax, &tay, &tbx, &tby);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], kBoardOrigin, kBoardRadius);
        for (int otherSide = 0; otherSide < HEX_CORNERS; otherSide++)
        {
            int oax = 0;
            int oay = 0;
            int obx = 0;
            int oby = 0;
            get_edge_key(otherCenter, kBoardRadius, otherSide, &oax, &oay, &obx, &oby);
            if (edge_keys_match(tax, tay, tbx, tby, oax, oay, obx, oby))
            {
                map->tiles[otherTile].sides[otherSide].isset = true;
                map->tiles[otherTile].sides[otherSide].player = player;
            }
        }
    }
}

static bool has_pending_builds_for_current_player(const struct ConsoleState *state)
{
    const enum PlayerType player = state->map.currentPlayer;

    if (player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return false;
    }

    return state->pendingRoad[player] > 0 ||
           state->pendingSettlement[player] > 0 ||
           state->pendingCity[player] > 0;
}

static bool try_buy_build(struct ConsoleState *state, enum BuildType buildType)
{
    const enum PlayerType player = state->map.currentPlayer;

    if (state->map.phase != GAME_PHASE_PLAY)
    {
        printf("buy is only available in play phase\n");
        return false;
    }

    switch (buildType)
    {
    case BUILD_ROAD:
        if (!gameTryBuyRoad(&state->map))
        {
            printf("cannot afford road\n");
            return false;
        }
        state->pendingRoad[player]++;
        return true;
    case BUILD_SETTLEMENT:
        if (!gameTryBuySettlement(&state->map))
        {
            printf("cannot afford settlement\n");
            return false;
        }
        state->pendingSettlement[player]++;
        return true;
    case BUILD_CITY:
        if (!gameTryBuyCity(&state->map))
        {
            printf("cannot afford city\n");
            return false;
        }
        state->pendingCity[player]++;
        return true;
    default:
        return false;
    }
}

static bool try_place_settlement(struct ConsoleState *state, int tileId, int cornerIndex)
{
    const bool setupMode = gameIsSetupSettlementTurn(&state->map);
    const enum PlayerType player = state->map.currentPlayer;

    if (tileId < 0 || tileId >= LAND_TILE_COUNT || cornerIndex < 0 || cornerIndex >= HEX_CORNERS)
    {
        printf("invalid tile/corner\n");
        return false;
    }

    if (!boardIsValidSettlementPlacement(&state->map, tileId, cornerIndex, player, kBoardOrigin, kBoardRadius))
    {
        printf("invalid settlement placement\n");
        return false;
    }

    if (!setupMode && state->pendingSettlement[player] <= 0)
    {
        printf("buy settlement first (buy settlement)\n");
        return false;
    }

    apply_settlement_on_shared_corner(&state->map, tileId, cornerIndex, player, STRUCTURE_TOWN);
    gameRefreshAwards(&state->map);
    if (setupMode)
    {
        gameHandlePlacedSettlement(&state->map, tileId, cornerIndex);
    }
    else
    {
        state->pendingSettlement[player]--;
        gameCheckVictory(&state->map, player);
    }

    return true;
}

static bool try_place_city(struct ConsoleState *state, int tileId, int cornerIndex)
{
    const enum PlayerType player = state->map.currentPlayer;

    if (state->map.phase != GAME_PHASE_PLAY)
    {
        printf("city placement is only available in play phase\n");
        return false;
    }

    if (tileId < 0 || tileId >= LAND_TILE_COUNT || cornerIndex < 0 || cornerIndex >= HEX_CORNERS)
    {
        printf("invalid tile/corner\n");
        return false;
    }

    if (state->pendingCity[player] <= 0)
    {
        printf("buy city first (buy city)\n");
        return false;
    }

    if (!boardIsValidCityPlacement(&state->map, tileId, cornerIndex, player))
    {
        printf("invalid city placement\n");
        return false;
    }

    apply_settlement_on_shared_corner(&state->map, tileId, cornerIndex, player, STRUCTURE_CITY);
    state->pendingCity[player]--;
    gameRefreshAwards(&state->map);
    gameCheckVictory(&state->map, player);
    return true;
}

static bool try_place_road(struct ConsoleState *state, int tileId, int sideIndex)
{
    const bool setupMode = gameIsSetupRoadTurn(&state->map);
    const bool hasFreeRoad = gameHasFreeRoadPlacements(&state->map);
    const enum PlayerType player = state->map.currentPlayer;

    if (tileId < 0 || tileId >= LAND_TILE_COUNT || sideIndex < 0 || sideIndex >= HEX_CORNERS)
    {
        printf("invalid tile/side\n");
        return false;
    }

    if (!boardIsValidRoadPlacement(&state->map, tileId, sideIndex, player, kBoardOrigin, kBoardRadius))
    {
        printf("invalid road placement\n");
        return false;
    }

    if (setupMode &&
        !boardEdgeTouchesCorner(tileId,
                                sideIndex,
                                state->map.setupSettlementTileId,
                                state->map.setupSettlementCornerIndex,
                                kBoardOrigin,
                                kBoardRadius))
    {
        printf("setup road must touch your latest setup settlement\n");
        return false;
    }

    if (!setupMode && !hasFreeRoad && state->pendingRoad[player] <= 0)
    {
        printf("buy road first (buy road)\n");
        return false;
    }

    apply_road_on_shared_edge(&state->map, tileId, sideIndex, player);
    gameRefreshAwards(&state->map);

    if (setupMode)
    {
        gameHandlePlacedRoad(&state->map);
    }
    else if (hasFreeRoad)
    {
        gameConsumeFreeRoadPlacement(&state->map);
        gameCheckVictory(&state->map, player);
    }
    else
    {
        state->pendingRoad[player]--;
        gameCheckVictory(&state->map, player);
    }

    return true;
}

static void print_help(void)
{
    printf("Commands:\n");
    printf("  help\n");
    printf("  status\n");
    printf("  board\n");
    printf("  hands [all]\n");
    printf("  bank\n");
    printf("  private on|off\n");
    printf("  view <player|current>\n");
    printf("  roll [2-12]\n");
    printf("  end\n");
    printf("  buy road|settlement|city|dev\n");
    printf("  place settlement <tileId> <cornerIndex>\n");
    printf("  place road <tileId> <sideIndex>\n");
    printf("  place city <tileId> <cornerIndex>\n");
    printf("  upgrade city <tileId> <cornerIndex>\n");
    printf("  discard <player> <wood> <wheat> <clay> <sheep> <stone>\n");
    printf("  move_thief <tileId>\n");
    printf("  steal <player>\n");
    printf("  play knight\n");
    printf("  play road_building\n");
    printf("  play year_of_plenty <res1> <res2>\n");
    printf("  play monopoly <resource>\n");
    printf("  maritime <give> <count> <receive>\n");
    printf("  trade <otherPlayer> <give> <giveAmount> <receive> <receiveAmount>\n");
    printf("  seed <int>\n");
    printf("  quit\n");
    printf("Resources: wood wheat clay sheep stone\n");
    printf("Players: 0..3 or red blue green black\n");
}

static void print_status(const struct ConsoleState *state)
{
    const struct Map *map = &state->map;
    const enum PlayerType player = map->currentPlayer;
    const enum PlayerType shownPlayer = shown_player(state);

    printf("phase=%s current=%s(%d) winner=%s last_roll=%d\n",
           phase_name(map->phase),
           player_name(player),
           (int)player,
           player_name(gameGetWinner(map)),
           map->lastDiceRoll);

    printf("flags: rolled=%d pending_discards=%d thief_place=%d thief_victim=%d free_road=%d\n",
           map->rolledThisTurn ? 1 : 0,
           gameHasPendingDiscards(map) ? 1 : 0,
           gameNeedsThiefPlacement(map) ? 1 : 0,
           gameNeedsThiefVictimSelection(map) ? 1 : 0,
           gameGetFreeRoadPlacementsRemaining(map));

    if (map->phase == GAME_PHASE_SETUP)
    {
        printf("setup: step=%d needs_road=%d latest_settlement=(tile=%d corner=%d)\n",
               map->setupStep,
               map->setupNeedsRoad ? 1 : 0,
               map->setupSettlementTileId,
               map->setupSettlementCornerIndex);
    }

    if (player >= PLAYER_RED && player <= PLAYER_BLACK)
    {
        printf("pending builds for current: road=%d settlement=%d city=%d\n",
               state->pendingRoad[player],
               state->pendingSettlement[player],
               state->pendingCity[player]);
    }

    printf("view: private=%d shown=%s\n", state->privateViewEnabled ? 1 : 0, player_name(shownPlayer));

    for (int p = PLAYER_RED; p <= PLAYER_BLACK; p++)
    {
        if (state->privateViewEnabled && p != shownPlayer)
        {
            int handTotal = 0;
            int devTotal = 0;
            for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
            {
                handTotal += map->players[p].resources[resource];
            }
            for (int type = 0; type < DEVELOPMENT_CARD_COUNT; type++)
            {
                devTotal += map->players[p].developmentCards[type];
            }

            printf("%s vp=%d vis=%d hidden(hand=%d dev=%d)\n",
                   player_name((enum PlayerType)p),
                   gameComputeVictoryPoints(map, (enum PlayerType)p),
                   gameComputeVisibleVictoryPoints(map, (enum PlayerType)p),
                   handTotal,
                   devTotal);
        }
        else
        {
            printf("%s vp=%d vis=%d hand=[w:%d wh:%d c:%d s:%d st:%d] dev=[k:%d vp:%d rb:%d yop:%d m:%d]\n",
                   player_name((enum PlayerType)p),
                   gameComputeVictoryPoints(map, (enum PlayerType)p),
                   gameComputeVisibleVictoryPoints(map, (enum PlayerType)p),
                   map->players[p].resources[RESOURCE_WOOD],
                   map->players[p].resources[RESOURCE_WHEAT],
                   map->players[p].resources[RESOURCE_CLAY],
                   map->players[p].resources[RESOURCE_SHEEP],
                   map->players[p].resources[RESOURCE_STONE],
                   map->players[p].developmentCards[DEVELOPMENT_CARD_KNIGHT],
                   map->players[p].developmentCards[DEVELOPMENT_CARD_VICTORY_POINT],
                   map->players[p].developmentCards[DEVELOPMENT_CARD_ROAD_BUILDING],
                   map->players[p].developmentCards[DEVELOPMENT_CARD_YEAR_OF_PLENTY],
                   map->players[p].developmentCards[DEVELOPMENT_CARD_MONOPOLY]);
        }
    }

    printf("awards: largest_army=%s longest_road=%s (%d)\n",
           player_name(gameGetLargestArmyOwner(map)),
           player_name(gameGetLongestRoadOwner(map)),
           gameGetLongestRoadLength(map));
}

static void print_board(const struct ConsoleState *state)
{
    const struct Map *map = &state->map;

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        int towns = 0;
        int cities = 0;
        int roads = 0;

        for (int corner = 0; corner < HEX_CORNERS; corner++)
        {
            if (map->tiles[tileId].corners[corner].structure == STRUCTURE_TOWN)
            {
                towns++;
            }
            else if (map->tiles[tileId].corners[corner].structure == STRUCTURE_CITY)
            {
                cities++;
            }
        }
        for (int side = 0; side < HEX_CORNERS; side++)
        {
            if (map->tiles[tileId].sides[side].isset)
            {
                roads++;
            }
        }

        printf("tile=%2d type=%-8s dice=%2d thief=%d corners(town=%d city=%d) roads=%d\n",
               tileId,
               tile_type_name(map->tiles[tileId].type),
               map->tiles[tileId].diceNumber,
               map->thiefTileId == tileId ? 1 : 0,
               towns,
               cities,
               roads);
    }
}

static void print_hands(const struct ConsoleState *state, bool allPlayers)
{
    const struct Map *map = &state->map;
    const enum PlayerType shownPlayer = shown_player(state);

    for (int p = PLAYER_RED; p <= PLAYER_BLACK; p++)
    {
        if (state->privateViewEnabled && !allPlayers && p != shownPlayer)
        {
            continue;
        }

        printf("%s: wood=%d wheat=%d clay=%d sheep=%d stone=%d\n",
               player_name((enum PlayerType)p),
               map->players[p].resources[RESOURCE_WOOD],
               map->players[p].resources[RESOURCE_WHEAT],
               map->players[p].resources[RESOURCE_CLAY],
               map->players[p].resources[RESOURCE_SHEEP],
               map->players[p].resources[RESOURCE_STONE]);
    }
}

static void print_bank(const struct ConsoleState *state)
{
    printf("bank: wood=%d wheat=%d clay=%d sheep=%d stone=%d\n",
           gameGetBankResourceCount(&state->map, RESOURCE_WOOD),
           gameGetBankResourceCount(&state->map, RESOURCE_WHEAT),
           gameGetBankResourceCount(&state->map, RESOURCE_CLAY),
           gameGetBankResourceCount(&state->map, RESOURCE_SHEEP),
           gameGetBankResourceCount(&state->map, RESOURCE_STONE));
}

int main(void)
{
    struct ConsoleState state;
    char line[INPUT_LINE_MAX];

    memset(&state, 0, sizeof(state));
    srand((unsigned int)time(NULL));

    if (!setupMap(&state.map))
    {
        fprintf(stderr, "failed to initialize map\n");
        return 1;
    }
    state.privateViewEnabled = true;
    state.revealedPlayer = state.map.currentPlayer;

    printf("SoC Console Mode\n");
    printf("Type 'help' for commands.\n");
    print_status(&state);

    while (true)
    {
        char *tokens[16] = {0};
        int tokenCount = 0;

        printf("soc[%s]> ", player_name(state.map.currentPlayer));
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            printf("\n");
            break;
        }

        {
            char *cursor = strtok(line, " \t\r\n");
            while (cursor != NULL && tokenCount < (int)(sizeof(tokens) / sizeof(tokens[0])))
            {
                tokens[tokenCount++] = cursor;
                cursor = strtok(NULL, " \t\r\n");
            }
        }

        if (tokenCount == 0)
        {
            continue;
        }

        if (equals_ignore_case(tokens[0], "quit") || equals_ignore_case(tokens[0], "exit"))
        {
            break;
        }
        else if (equals_ignore_case(tokens[0], "help"))
        {
            print_help();
        }
        else if (equals_ignore_case(tokens[0], "status"))
        {
            print_status(&state);
        }
        else if (equals_ignore_case(tokens[0], "board"))
        {
            print_board(&state);
        }
        else if (equals_ignore_case(tokens[0], "hands"))
        {
            print_hands(&state, tokenCount >= 2 && equals_ignore_case(tokens[1], "all"));
        }
        else if (equals_ignore_case(tokens[0], "bank"))
        {
            print_bank(&state);
        }
        else if (equals_ignore_case(tokens[0], "private"))
        {
            if (tokenCount < 2)
            {
                printf("usage: private on|off\n");
                continue;
            }

            if (equals_ignore_case(tokens[1], "on"))
            {
                state.privateViewEnabled = true;
                state.revealedPlayer = state.map.currentPlayer;
                printf("private view enabled\n");
            }
            else if (equals_ignore_case(tokens[1], "off"))
            {
                state.privateViewEnabled = false;
                printf("private view disabled\n");
            }
            else
            {
                printf("usage: private on|off\n");
            }
        }
        else if (equals_ignore_case(tokens[0], "view"))
        {
            enum PlayerType target = PLAYER_NONE;

            if (tokenCount < 2 || !parse_view_target(&state, tokens[1], &target))
            {
                printf("usage: view <player|current>\n");
                continue;
            }

            state.revealedPlayer = target;
            printf("revealed player set to %s\n", player_name(target));
        }
        else if (equals_ignore_case(tokens[0], "seed"))
        {
            int seedValue = 0;
            if (tokenCount < 2 || !parse_int(tokens[1], &seedValue))
            {
                printf("usage: seed <int>\n");
            }
            else
            {
                srand((unsigned int)seedValue);
                printf("random seed set to %d\n", seedValue);
            }
        }
        else if (equals_ignore_case(tokens[0], "roll"))
        {
            int roll = 0;
            if (tokenCount >= 2)
            {
                if (!parse_int(tokens[1], &roll) || roll < 2 || roll > 12)
                {
                    printf("usage: roll [2-12]\n");
                    continue;
                }
            }
            else
            {
                roll = roll_two_dice();
            }

            if (!gameCanRollDice(&state.map))
            {
                printf("cannot roll now\n");
                continue;
            }

            gameRollDice(&state.map, roll);
            printf("rolled %d\n", roll);
            if (gameHasPendingDiscards(&state.map))
            {
                printf("discard required for %s\n", player_name(gameGetCurrentDiscardPlayer(&state.map)));
            }
        }
        else if (equals_ignore_case(tokens[0], "end"))
        {
            if (has_pending_builds_for_current_player(&state))
            {
                printf("you still have pending bought builds to place\n");
                continue;
            }

            if (!gameCanEndTurn(&state.map))
            {
                printf("cannot end turn now\n");
                continue;
            }

            gameEndTurn(&state.map);
            if (state.privateViewEnabled)
            {
                state.revealedPlayer = state.map.currentPlayer;
            }
            printf("turn ended\n");
        }
        else if (equals_ignore_case(tokens[0], "buy"))
        {
            enum BuildType buildType = BUILD_ROAD;
            enum DevelopmentCardType drawn = DEVELOPMENT_CARD_COUNT;

            if (tokenCount < 7)
            {
                printf("usage: buy road|settlement|city|dev\n");
                continue;
            }

            if (equals_ignore_case(tokens[1], "dev") || equals_ignore_case(tokens[1], "development"))
            {
                if (!gameTryBuyDevelopment(&state.map, &drawn))
                {
                    printf("cannot buy development card\n");
                    continue;
                }
                printf("bought development card: %s\n", dev_card_name(drawn));
            }
            else if (parse_build_type(tokens[1], &buildType))
            {
                if (!try_buy_build(&state, buildType))
                {
                    continue;
                }
                printf("bought %s\n", tokens[1]);
            }
            else
            {
                printf("usage: buy road|settlement|city|dev\n");
            }
        }
        else if (equals_ignore_case(tokens[0], "place"))
        {
            int tileId = 0;
            int index = 0;

            if (tokenCount < 4)
            {
                printf("usage: place settlement|road|city <tileId> <cornerOrSideIndex>\n");
                continue;
            }

            if (!parse_int(tokens[2], &tileId) || !parse_int(tokens[3], &index))
            {
                printf("tile/index must be integers\n");
                continue;
            }

            if (equals_ignore_case(tokens[1], "settlement") || equals_ignore_case(tokens[1], "town"))
            {
                if (try_place_settlement(&state, tileId, index))
                {
                    printf("settlement placed\n");
                }
            }
            else if (equals_ignore_case(tokens[1], "road"))
            {
                if (try_place_road(&state, tileId, index))
                {
                    printf("road placed\n");
                }
            }
            else if (equals_ignore_case(tokens[1], "city"))
            {
                if (try_place_city(&state, tileId, index))
                {
                    printf("city placed\n");
                }
            }
            else
            {
                printf("usage: place settlement|road|city <tileId> <cornerOrSideIndex>\n");
            }
        }
        else if (equals_ignore_case(tokens[0], "upgrade"))
        {
            int tileId = 0;
            int corner = 0;

            if (tokenCount < 4 || !equals_ignore_case(tokens[1], "city") ||
                !parse_int(tokens[2], &tileId) || !parse_int(tokens[3], &corner))
            {
                printf("usage: upgrade city <tileId> <cornerIndex>\n");
                continue;
            }

            if (try_place_city(&state, tileId, corner))
            {
                printf("city placed\n");
            }
        }
        else if (equals_ignore_case(tokens[0], "discard"))
        {
            enum PlayerType player = PLAYER_NONE;
            int discard[5] = {0};

            if (tokenCount < 8)
            {
                printf("usage: discard <player> <wood> <wheat> <clay> <sheep> <stone>\n");
                continue;
            }

            if (!parse_player(tokens[1], &player))
            {
                printf("invalid player\n");
                continue;
            }

            if (!parse_int(tokens[2], &discard[RESOURCE_WOOD]) ||
                !parse_int(tokens[3], &discard[RESOURCE_WHEAT]) ||
                !parse_int(tokens[4], &discard[RESOURCE_CLAY]) ||
                !parse_int(tokens[5], &discard[RESOURCE_SHEEP]) ||
                !parse_int(tokens[6], &discard[RESOURCE_STONE]))
            {
                printf("discard counts must be integers\n");
                continue;
            }

            if (!gameTrySubmitDiscard(&state.map, player, discard))
            {
                printf("discard rejected\n");
                continue;
            }

            printf("discard accepted\n");
        }
        else if (equals_ignore_case(tokens[0], "move_thief"))
        {
            int tileId = 0;
            if (tokenCount < 2 || !parse_int(tokens[1], &tileId))
            {
                printf("usage: move_thief <tileId>\n");
                continue;
            }

            if (!gameCanMoveThiefToTile(&state.map, tileId))
            {
                printf("cannot move thief there now\n");
                continue;
            }

            gameMoveThief(&state.map, tileId);
            printf("thief moved to tile %d\n", tileId);
        }
        else if (equals_ignore_case(tokens[0], "steal"))
        {
            enum PlayerType victim = PLAYER_NONE;
            enum ResourceType stolen = RESOURCE_WOOD;

            if (tokenCount < 2 || !parse_player(tokens[1], &victim))
            {
                printf("usage: steal <player>\n");
                continue;
            }

            if (!gameStealRandomResourceDetailed(&state.map, victim, &stolen))
            {
                printf("cannot steal from %s now\n", player_name(victim));
                continue;
            }

            printf("stole %s from %s\n", resource_name(stolen), player_name(victim));
        }
        else if (equals_ignore_case(tokens[0], "play"))
        {
            enum DevelopmentCardType card = DEVELOPMENT_CARD_COUNT;

            if (tokenCount < 2 || !parse_dev_play_type(tokens[1], &card))
            {
                printf("usage: play knight|road_building|year_of_plenty|monopoly ...\n");
                continue;
            }

            if (card == DEVELOPMENT_CARD_KNIGHT)
            {
                if (!gameTryPlayKnight(&state.map))
                {
                    printf("cannot play knight now\n");
                    continue;
                }
                printf("played knight\n");
            }
            else if (card == DEVELOPMENT_CARD_ROAD_BUILDING)
            {
                if (!gameTryPlayRoadBuilding(&state.map))
                {
                    printf("cannot play road building now\n");
                    continue;
                }
                printf("played road building (2 free roads)\n");
            }
            else if (card == DEVELOPMENT_CARD_YEAR_OF_PLENTY)
            {
                enum ResourceType first = RESOURCE_WOOD;
                enum ResourceType second = RESOURCE_WOOD;

                if (tokenCount < 4 || !parse_resource(tokens[2], &first) || !parse_resource(tokens[3], &second))
                {
                    printf("usage: play year_of_plenty <res1> <res2>\n");
                    continue;
                }

                if (!gameTryPlayYearOfPlenty(&state.map, first, second))
                {
                    printf("cannot play year of plenty with selected resources\n");
                    continue;
                }
                printf("played year of plenty\n");
            }
            else if (card == DEVELOPMENT_CARD_MONOPOLY)
            {
                enum ResourceType target = RESOURCE_WOOD;

                if (tokenCount < 3 || !parse_resource(tokens[2], &target))
                {
                    printf("usage: play monopoly <resource>\n");
                    continue;
                }

                if (!gameTryPlayMonopoly(&state.map, target))
                {
                    printf("cannot play monopoly now\n");
                    continue;
                }
                printf("played monopoly for %s\n", resource_name(target));
            }
        }
        else if (equals_ignore_case(tokens[0], "maritime"))
        {
            enum ResourceType give = RESOURCE_WOOD;
            enum ResourceType receive = RESOURCE_WOOD;
            int count = 0;

            if (tokenCount < 4 || !parse_resource(tokens[1], &give) || !parse_int(tokens[2], &count) || !parse_resource(tokens[3], &receive))
            {
                printf("usage: maritime <give> <count> <receive>\n");
                continue;
            }

            if (!gameTryTradeMaritime(&state.map, give, count, receive))
            {
                printf("maritime trade rejected\n");
                continue;
            }

            printf("maritime trade complete\n");
        }
        else if (equals_ignore_case(tokens[0], "trade"))
        {
            enum PlayerType other = PLAYER_NONE;
            enum ResourceType give = RESOURCE_WOOD;
            enum ResourceType receive = RESOURCE_WOOD;
            int giveAmount = 0;
            int receiveAmount = 0;

            if (tokenCount < 6 ||
                !parse_player(tokens[1], &other) ||
                !parse_resource(tokens[2], &give) ||
                !parse_int(tokens[3], &giveAmount) ||
                !parse_resource(tokens[4], &receive) ||
                !parse_int(tokens[5], &receiveAmount))
            {
                printf("usage: trade <otherPlayer> <give> <giveAmount> <receive> <receiveAmount>\n");
                continue;
            }

            if (!gameTryTradeWithPlayer(&state.map, other, give, giveAmount, receive, receiveAmount))
            {
                printf("trade rejected\n");
                continue;
            }

            printf("trade complete\n");
        }
        else
        {
            printf("unknown command. type 'help'\n");
        }

        if (gameHasWinner(&state.map))
        {
            printf("game over: winner is %s\n", player_name(gameGetWinner(&state.map)));
        }
    }

    return 0;
}
