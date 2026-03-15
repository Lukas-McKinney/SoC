#include "board_rules.h"
#include "game_logic.h"
#include "map.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LAND_TILE_COUNT 19
#define HEX_CORNERS 6
#define TOPOLOGY_MAX_NODES 72
#define TOPOLOGY_MAX_EDGES (LAND_TILE_COUNT * HEX_CORNERS)

struct AxialCoord
{
    int q;
    int r;
};

struct NodeRef
{
    int x;
    int y;
    int tileId;
    int cornerIndex;
};

struct EdgeRef
{
    int ax;
    int ay;
    int bx;
    int by;
    int tileId;
    int sideIndex;
    int nodeA;
    int nodeB;
};

struct Topology
{
    struct NodeRef nodes[TOPOLOGY_MAX_NODES];
    int nodeCount;
    struct EdgeRef edges[TOPOLOGY_MAX_EDGES];
    int edgeCount;
};

struct PathRef
{
    int nodeIndices[TOPOLOGY_MAX_EDGES + 1];
    int edgeIndices[TOPOLOGY_MAX_EDGES];
    int edgeCount;
};

static const struct AxialCoord kLandCoords[LAND_TILE_COUNT] = {
    {0, -2}, {1, -2}, {2, -2}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}, {-2, 2}, {-1, 2}, {0, 2}};
static const Vector2 kBoardOrigin = {1600.0f * 0.42f, 900.0f * 0.46f};
static const float kBoardRadius = 68.0f;

static int gFailureCount = 0;
static int gTestCount = 0;

static Vector2 axial_to_world(struct AxialCoord coord, Vector2 origin, float radius);
static Vector2 point_on_hex(Vector2 center, float radius, int cornerIndex);
static void get_corner_key(Vector2 center, float radius, int cornerIndex, int *x, int *y);
static void get_side_corner_indices(int sideIndex, int *cornerA, int *cornerB);
static void get_edge_key(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by);
static bool corner_keys_match(int x1, int y1, int x2, int y2);
static void fail_assertion(const char *testName, int line, const char *message);
static void initialize_test_map(struct Map *map);
static void zero_all_resources(struct Map *map);
static void build_topology(struct Topology *topology);
static int find_node_index(const struct Topology *topology, int x, int y);
static int find_edge_index(const struct Topology *topology, int ax, int ay, int bx, int by);
static int get_adjacent_tiles_for_node(const struct Topology *topology, int nodeIndex, int tileIds[3]);
static void place_structure_on_node(struct Map *map, const struct Topology *topology, int nodeIndex, enum PlayerType owner, enum StructureType structure);
static void place_road_on_edge(struct Map *map, const struct Topology *topology, int edgeIndex, enum PlayerType owner);
static bool find_path_with_constraints(const struct Topology *topology, int desiredEdges,
                                       const bool forbiddenEdges[TOPOLOGY_MAX_EDGES],
                                       const bool forbiddenNodes[TOPOLOGY_MAX_NODES],
                                       struct PathRef *path);
static bool find_path_from_node(const struct Topology *topology, int currentNode, int desiredEdges,
                                const bool forbiddenEdges[TOPOLOGY_MAX_EDGES],
                                const bool forbiddenNodes[TOPOLOGY_MAX_NODES],
                                bool usedEdges[TOPOLOGY_MAX_EDGES], bool visitedNodes[TOPOLOGY_MAX_NODES],
                                struct PathRef *working, int depth,
                                struct PathRef *result);
static void mark_path_edges(const struct PathRef *path, bool usedEdges[TOPOLOGY_MAX_EDGES]);
static void apply_path_roads(struct Map *map, const struct Topology *topology, const struct PathRef *path, int roadCount, enum PlayerType owner);

static bool test_setup_settlement_requires_distance_rule(void);
static bool test_play_roads_must_connect_to_network(void);
static bool test_road_cannot_continue_only_through_opponent_settlement(void);
static bool test_second_setup_settlement_grants_each_adjacent_non_desert_resource(void);
static bool test_roll_pays_all_structures_and_thief_blocks_the_tile(void);
static bool test_development_cards_are_locked_until_next_turn_and_limited_to_one_play(void);
static bool test_largest_army_requires_three_and_transfers_only_on_strictly_more_knights(void);
static bool test_longest_road_requires_five_and_current_owner_keeps_tied_length(void);
static bool test_longest_road_transfers_to_a_longer_network(void);
static bool test_longest_road_breaks_when_an_opponent_settlement_blocks_the_path(void);
static bool test_city_upgrade_requires_an_owned_town(void);

#define ASSERT_TRUE(expr)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            fail_assertion(__func__, __LINE__, #expr);                                                                 \
            return false;                                                                                              \
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
            char buffer[128];                                                                                          \
            snprintf(buffer, sizeof(buffer), "%s expected %d but was %d", #actual, expectedValue, actualValue);       \
            fail_assertion(__func__, __LINE__, buffer);                                                                \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

int main(void)
{
    const struct
    {
        const char *name;
        bool (*fn)(void);
    } tests[] = {
        {"setup settlement distance rule", test_setup_settlement_requires_distance_rule},
        {"roads connect to existing network", test_play_roads_must_connect_to_network},
        {"roads blocked by opponent settlement", test_road_cannot_continue_only_through_opponent_settlement},
        {"second setup settlement grants resources", test_second_setup_settlement_grants_each_adjacent_non_desert_resource},
        {"roll payout and thief blocking", test_roll_pays_all_structures_and_thief_blocks_the_tile},
        {"development card timing limits", test_development_cards_are_locked_until_next_turn_and_limited_to_one_play},
        {"largest army threshold and transfer", test_largest_army_requires_three_and_transfers_only_on_strictly_more_knights},
        {"longest road threshold and tie retention", test_longest_road_requires_five_and_current_owner_keeps_tied_length},
        {"longest road transfers to longer network", test_longest_road_transfers_to_a_longer_network},
        {"longest road blocked by opponent settlement", test_longest_road_breaks_when_an_opponent_settlement_blocks_the_path},
        {"city upgrade requires owned town", test_city_upgrade_requires_an_owned_town},
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
        }
    }

    printf("\n%d/%d rule tests passed\n", gTestCount - gFailureCount, gTestCount);
    return gFailureCount == 0 ? 0 : 1;
}

static bool test_setup_settlement_requires_distance_rule(void)
{
    struct Map map;
    struct Topology topology;
    initialize_test_map(&map);
    build_topology(&topology);

    map.phase = GAME_PHASE_SETUP;
    map.setupNeedsRoad = false;

    const struct EdgeRef *edge = &topology.edges[0];
    place_structure_on_node(&map, &topology, edge->nodeA, PLAYER_RED, STRUCTURE_TOWN);

    {
        const struct NodeRef *adjacentCorner = &topology.nodes[edge->nodeB];
        ASSERT_FALSE(boardIsValidSettlementPlacement(&map, adjacentCorner->tileId, adjacentCorner->cornerIndex,
                                                     PLAYER_BLUE, kBoardOrigin, kBoardRadius));
    }
    return true;
}

static bool test_play_roads_must_connect_to_network(void)
{
    struct Map map;
    struct Topology topology;
    initialize_test_map(&map);
    build_topology(&topology);

    map.phase = GAME_PHASE_PLAY;

    {
        const struct EdgeRef *targetEdge = &topology.edges[0];
        ASSERT_FALSE(boardIsValidRoadPlacement(&map, targetEdge->tileId, targetEdge->sideIndex,
                                               PLAYER_RED, kBoardOrigin, kBoardRadius));

        place_structure_on_node(&map, &topology, targetEdge->nodeA, PLAYER_RED, STRUCTURE_TOWN);
        ASSERT_TRUE(boardIsValidRoadPlacement(&map, targetEdge->tileId, targetEdge->sideIndex,
                                              PLAYER_RED, kBoardOrigin, kBoardRadius));
    }
    return true;
}

static bool test_road_cannot_continue_only_through_opponent_settlement(void)
{
    struct Map map;
    struct Topology topology;
    struct PathRef path;
    initialize_test_map(&map);
    build_topology(&topology);

    map.phase = GAME_PHASE_PLAY;
    ASSERT_TRUE(find_path_with_constraints(&topology, 2, NULL, NULL, &path));

    place_structure_on_node(&map, &topology, path.nodeIndices[0], PLAYER_RED, STRUCTURE_TOWN);
    place_road_on_edge(&map, &topology, path.edgeIndices[0], PLAYER_RED);
    place_structure_on_node(&map, &topology, path.nodeIndices[1], PLAYER_BLUE, STRUCTURE_TOWN);

    {
        const struct EdgeRef *nextEdge = &topology.edges[path.edgeIndices[1]];
        ASSERT_FALSE(boardIsValidRoadPlacement(&map, nextEdge->tileId, nextEdge->sideIndex,
                                               PLAYER_RED, kBoardOrigin, kBoardRadius));
    }
    return true;
}

static bool test_second_setup_settlement_grants_each_adjacent_non_desert_resource(void)
{
    struct Map map;
    struct Topology topology;
    int adjacentTiles[3] = {-1, -1, -1};
    initialize_test_map(&map);
    build_topology(&topology);

    map.phase = GAME_PHASE_SETUP;
    map.currentPlayer = PLAYER_BLUE;
    map.setupStep = MAX_PLAYERS;
    map.setupNeedsRoad = false;
    zero_all_resources(&map);

    {
        int targetNode = -1;
        for (int nodeIndex = 0; nodeIndex < topology.nodeCount; nodeIndex++)
        {
            if (get_adjacent_tiles_for_node(&topology, nodeIndex, adjacentTiles) == 3)
            {
                targetNode = nodeIndex;
                break;
            }
        }

        ASSERT_TRUE(targetNode >= 0);
        for (int tileId = 0; tileId < MAX_TILES; tileId++)
        {
            map.tiles[tileId].type = TILE_DESERT;
        }
        map.tiles[adjacentTiles[0]].type = TILE_FOREST;
        map.tiles[adjacentTiles[1]].type = TILE_MOUNTAIN;
        map.tiles[adjacentTiles[2]].type = TILE_DESERT;

        {
            const struct NodeRef *corner = &topology.nodes[targetNode];
            gameHandlePlacedSettlement(&map, corner->tileId, corner->cornerIndex);
        }
    }

    ASSERT_EQ_INT(1, map.players[PLAYER_BLUE].resources[RESOURCE_WOOD]);
    ASSERT_EQ_INT(1, map.players[PLAYER_BLUE].resources[RESOURCE_STONE]);
    ASSERT_EQ_INT(0, map.players[PLAYER_BLUE].resources[RESOURCE_WHEAT]);
    ASSERT_EQ_INT(0, map.players[PLAYER_BLUE].resources[RESOURCE_CLAY]);
    ASSERT_EQ_INT(0, map.players[PLAYER_BLUE].resources[RESOURCE_SHEEP]);
    ASSERT_TRUE(map.setupNeedsRoad);
    return true;
}

static bool test_roll_pays_all_structures_and_thief_blocks_the_tile(void)
{
    struct Map map;
    initialize_test_map(&map);
    zero_all_resources(&map);

    map.phase = GAME_PHASE_PLAY;
    map.thiefTileId = 9;
    map.tiles[0].type = TILE_FOREST;
    map.tiles[0].diceNumber = 8;
    map.tiles[0].corners[0].owner = PLAYER_RED;
    map.tiles[0].corners[0].structure = STRUCTURE_TOWN;
    map.tiles[0].corners[2].owner = PLAYER_BLUE;
    map.tiles[0].corners[2].structure = STRUCTURE_TOWN;
    map.tiles[0].corners[4].owner = PLAYER_RED;
    map.tiles[0].corners[4].structure = STRUCTURE_CITY;

    gameRollDice(&map, 8);
    ASSERT_EQ_INT(3, map.players[PLAYER_RED].resources[RESOURCE_WOOD]);
    ASSERT_EQ_INT(1, map.players[PLAYER_BLUE].resources[RESOURCE_WOOD]);

    zero_all_resources(&map);
    map.rolledThisTurn = false;
    map.lastDiceRoll = 0;
    map.thiefTileId = 0;
    gameRollDice(&map, 8);
    ASSERT_EQ_INT(0, map.players[PLAYER_RED].resources[RESOURCE_WOOD]);
    ASSERT_EQ_INT(0, map.players[PLAYER_BLUE].resources[RESOURCE_WOOD]);
    return true;
}

static bool test_development_cards_are_locked_until_next_turn_and_limited_to_one_play(void)
{
    struct Map map;
    enum DevelopmentCardType drawnCard = DEVELOPMENT_CARD_VICTORY_POINT;
    initialize_test_map(&map);
    zero_all_resources(&map);

    map.phase = GAME_PHASE_PLAY;
    map.currentPlayer = PLAYER_RED;
    map.developmentDeckCount = 1;
    map.developmentDeck[0] = DEVELOPMENT_CARD_KNIGHT;
    map.players[PLAYER_RED].resources[RESOURCE_WHEAT] = 1;
    map.players[PLAYER_RED].resources[RESOURCE_SHEEP] = 1;
    map.players[PLAYER_RED].resources[RESOURCE_STONE] = 1;

    ASSERT_TRUE(gameTryBuyDevelopment(&map, &drawnCard));
    ASSERT_EQ_INT(DEVELOPMENT_CARD_KNIGHT, drawnCard);
    ASSERT_FALSE(gameCanPlayDevelopmentCard(&map, DEVELOPMENT_CARD_KNIGHT));

    for (int turn = 0; turn < MAX_PLAYERS; turn++)
    {
        map.rolledThisTurn = true;
        map.awaitingThiefPlacement = false;
        map.awaitingThiefVictimSelection = false;
        gameEndTurn(&map);
    }

    ASSERT_TRUE(gameCanPlayDevelopmentCard(&map, DEVELOPMENT_CARD_KNIGHT));
    ASSERT_FALSE(gameCanPlayDevelopmentCard(&map, DEVELOPMENT_CARD_VICTORY_POINT));

    map.players[PLAYER_RED].developmentCards[DEVELOPMENT_CARD_YEAR_OF_PLENTY] = 1;
    map.players[PLAYER_RED].developmentCards[DEVELOPMENT_CARD_MONOPOLY] = 1;
    map.players[PLAYER_RED].newlyPurchasedDevelopmentCards[DEVELOPMENT_CARD_YEAR_OF_PLENTY] = 0;
    map.players[PLAYER_RED].newlyPurchasedDevelopmentCards[DEVELOPMENT_CARD_MONOPOLY] = 0;
    map.playedDevelopmentCardThisTurn = false;
    map.awaitingThiefPlacement = false;
    map.awaitingThiefVictimSelection = false;

    ASSERT_TRUE(gameTryPlayYearOfPlenty(&map, RESOURCE_WOOD, RESOURCE_STONE));
    ASSERT_FALSE(gameCanPlayDevelopmentCard(&map, DEVELOPMENT_CARD_MONOPOLY));
    ASSERT_EQ_INT(1, map.players[PLAYER_RED].resources[RESOURCE_WOOD]);
    ASSERT_EQ_INT(1, map.players[PLAYER_RED].resources[RESOURCE_STONE]);
    return true;
}

static bool test_largest_army_requires_three_and_transfers_only_on_strictly_more_knights(void)
{
    struct Map map;
    initialize_test_map(&map);
    map.phase = GAME_PHASE_PLAY;

    map.players[PLAYER_RED].developmentCards[DEVELOPMENT_CARD_KNIGHT] = 3;
    for (int i = 0; i < 3; i++)
    {
        map.currentPlayer = PLAYER_RED;
        map.playedDevelopmentCardThisTurn = false;
        map.awaitingThiefPlacement = false;
        map.awaitingThiefVictimSelection = false;
        ASSERT_TRUE(gameTryPlayKnight(&map));
        map.awaitingThiefPlacement = false;
        if (i < 2)
        {
            ASSERT_EQ_INT(PLAYER_NONE, gameGetLargestArmyOwner(&map));
        }
    }
    ASSERT_EQ_INT(PLAYER_RED, gameGetLargestArmyOwner(&map));

    map.players[PLAYER_BLUE].developmentCards[DEVELOPMENT_CARD_KNIGHT] = 4;
    for (int i = 0; i < 3; i++)
    {
        map.currentPlayer = PLAYER_BLUE;
        map.playedDevelopmentCardThisTurn = false;
        map.awaitingThiefPlacement = false;
        map.awaitingThiefVictimSelection = false;
        ASSERT_TRUE(gameTryPlayKnight(&map));
        map.awaitingThiefPlacement = false;
    }
    ASSERT_EQ_INT(PLAYER_RED, gameGetLargestArmyOwner(&map));

    map.currentPlayer = PLAYER_BLUE;
    map.playedDevelopmentCardThisTurn = false;
    map.awaitingThiefPlacement = false;
    map.awaitingThiefVictimSelection = false;
    ASSERT_TRUE(gameTryPlayKnight(&map));
    ASSERT_EQ_INT(PLAYER_BLUE, gameGetLargestArmyOwner(&map));
    return true;
}

static bool test_longest_road_requires_five_and_current_owner_keeps_tied_length(void)
{
    struct Map map;
    struct Topology topology;
    struct PathRef firstPath;
    struct PathRef secondPath;
    bool forbiddenEdges[TOPOLOGY_MAX_EDGES] = {0};
    initialize_test_map(&map);
    build_topology(&topology);
    map.phase = GAME_PHASE_PLAY;

    ASSERT_TRUE(find_path_with_constraints(&topology, 5, NULL, NULL, &firstPath));
    apply_path_roads(&map, &topology, &firstPath, 4, PLAYER_RED);
    gameRefreshAwards(&map);
    ASSERT_EQ_INT(PLAYER_NONE, gameGetLongestRoadOwner(&map));
    ASSERT_EQ_INT(4, gameGetLongestRoadLength(&map));

    apply_path_roads(&map, &topology, &firstPath, 5, PLAYER_RED);
    gameRefreshAwards(&map);
    ASSERT_EQ_INT(PLAYER_RED, gameGetLongestRoadOwner(&map));
    ASSERT_EQ_INT(5, gameGetLongestRoadLength(&map));

    mark_path_edges(&firstPath, forbiddenEdges);
    ASSERT_TRUE(find_path_with_constraints(&topology, 5, forbiddenEdges, NULL, &secondPath));
    apply_path_roads(&map, &topology, &secondPath, 5, PLAYER_BLUE);
    gameRefreshAwards(&map);
    ASSERT_EQ_INT(PLAYER_RED, gameGetLongestRoadOwner(&map));
    ASSERT_EQ_INT(5, gameGetLongestRoadLength(&map));
    return true;
}

static bool test_longest_road_transfers_to_a_longer_network(void)
{
    struct Map map;
    struct Topology topology;
    struct PathRef firstPath;
    struct PathRef secondPath;
    bool forbiddenEdges[TOPOLOGY_MAX_EDGES] = {0};
    initialize_test_map(&map);
    build_topology(&topology);
    map.phase = GAME_PHASE_PLAY;

    ASSERT_TRUE(find_path_with_constraints(&topology, 5, NULL, NULL, &firstPath));
    apply_path_roads(&map, &topology, &firstPath, 5, PLAYER_RED);
    gameRefreshAwards(&map);
    ASSERT_EQ_INT(PLAYER_RED, gameGetLongestRoadOwner(&map));

    mark_path_edges(&firstPath, forbiddenEdges);
    ASSERT_TRUE(find_path_with_constraints(&topology, 6, forbiddenEdges, NULL, &secondPath));
    apply_path_roads(&map, &topology, &secondPath, 6, PLAYER_BLUE);
    gameRefreshAwards(&map);
    ASSERT_EQ_INT(PLAYER_BLUE, gameGetLongestRoadOwner(&map));
    ASSERT_EQ_INT(6, gameGetLongestRoadLength(&map));
    return true;
}

static bool test_longest_road_breaks_when_an_opponent_settlement_blocks_the_path(void)
{
    struct Map map;
    struct Topology topology;
    struct PathRef path;
    initialize_test_map(&map);
    build_topology(&topology);
    map.phase = GAME_PHASE_PLAY;

    ASSERT_TRUE(find_path_with_constraints(&topology, 5, NULL, NULL, &path));
    apply_path_roads(&map, &topology, &path, 5, PLAYER_RED);
    gameRefreshAwards(&map);
    ASSERT_EQ_INT(PLAYER_RED, gameGetLongestRoadOwner(&map));

    place_structure_on_node(&map, &topology, path.nodeIndices[2], PLAYER_BLUE, STRUCTURE_TOWN);
    gameRefreshAwards(&map);
    ASSERT_EQ_INT(PLAYER_NONE, gameGetLongestRoadOwner(&map));
    ASSERT_TRUE(gameGetLongestRoadLength(&map) < 5);
    return true;
}

static bool test_city_upgrade_requires_an_owned_town(void)
{
    struct Map map;
    struct Topology topology;
    initialize_test_map(&map);
    build_topology(&topology);
    map.phase = GAME_PHASE_PLAY;

    place_structure_on_node(&map, &topology, 0, PLAYER_RED, STRUCTURE_TOWN);
    ASSERT_TRUE(boardIsValidCityPlacement(&map, topology.nodes[0].tileId, topology.nodes[0].cornerIndex, PLAYER_RED));
    ASSERT_FALSE(boardIsValidCityPlacement(&map, topology.nodes[0].tileId, topology.nodes[0].cornerIndex, PLAYER_BLUE));

    place_structure_on_node(&map, &topology, 0, PLAYER_RED, STRUCTURE_CITY);
    ASSERT_FALSE(boardIsValidCityPlacement(&map, topology.nodes[0].tileId, topology.nodes[0].cornerIndex, PLAYER_RED));
    return true;
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
        const int tempX = *ax;
        const int tempY = *ay;
        *ax = *bx;
        *ay = *by;
        *bx = tempX;
        *by = tempY;
    }
}

static bool corner_keys_match(int x1, int y1, int x2, int y2)
{
    return x1 == x2 && y1 == y2;
}

static void fail_assertion(const char *testName, int line, const char *message)
{
    fprintf(stderr, "[FAIL] %s:%d %s\n", testName, line, message);
}

static void initialize_test_map(struct Map *map)
{
    memset(map, 0, sizeof(*map));
    setupMap(map);
    map->phase = GAME_PHASE_PLAY;
    map->currentPlayer = PLAYER_RED;
    map->winner = PLAYER_NONE;
    map->rolledThisTurn = false;
    map->awaitingThiefPlacement = false;
    map->awaitingThiefVictimSelection = false;
    map->playedDevelopmentCardThisTurn = false;
    map->freeRoadPlacementsRemaining = 0;
    map->largestArmyOwner = PLAYER_NONE;
    map->longestRoadOwner = PLAYER_NONE;
    map->longestRoadLength = 0;
}

static void zero_all_resources(struct Map *map)
{
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        for (int resource = 0; resource < 5; resource++)
        {
            map->players[player].resources[resource] = 0;
        }
    }
}

static void build_topology(struct Topology *topology)
{
    memset(topology, 0, sizeof(*topology));

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        const Vector2 center = axial_to_world(kLandCoords[tileId], kBoardOrigin, kBoardRadius);
        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            int x = 0;
            int y = 0;
            get_corner_key(center, kBoardRadius, cornerIndex, &x, &y);
            if (find_node_index(topology, x, y) < 0)
            {
                const int nodeIndex = topology->nodeCount++;
                topology->nodes[nodeIndex].x = x;
                topology->nodes[nodeIndex].y = y;
                topology->nodes[nodeIndex].tileId = tileId;
                topology->nodes[nodeIndex].cornerIndex = cornerIndex;
            }
        }
    }

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        const Vector2 center = axial_to_world(kLandCoords[tileId], kBoardOrigin, kBoardRadius);
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            int ax = 0;
            int ay = 0;
            int bx = 0;
            int by = 0;
            get_edge_key(center, kBoardRadius, sideIndex, &ax, &ay, &bx, &by);
            if (find_edge_index(topology, ax, ay, bx, by) >= 0)
            {
                continue;
            }

            {
                const int edgeIndex = topology->edgeCount++;
                topology->edges[edgeIndex].ax = ax;
                topology->edges[edgeIndex].ay = ay;
                topology->edges[edgeIndex].bx = bx;
                topology->edges[edgeIndex].by = by;
                topology->edges[edgeIndex].tileId = tileId;
                topology->edges[edgeIndex].sideIndex = sideIndex;
                topology->edges[edgeIndex].nodeA = find_node_index(topology, ax, ay);
                topology->edges[edgeIndex].nodeB = find_node_index(topology, bx, by);
            }
        }
    }
}

static int find_node_index(const struct Topology *topology, int x, int y)
{
    for (int nodeIndex = 0; nodeIndex < topology->nodeCount; nodeIndex++)
    {
        if (corner_keys_match(topology->nodes[nodeIndex].x, topology->nodes[nodeIndex].y, x, y))
        {
            return nodeIndex;
        }
    }

    return -1;
}

static int find_edge_index(const struct Topology *topology, int ax, int ay, int bx, int by)
{
    for (int edgeIndex = 0; edgeIndex < topology->edgeCount; edgeIndex++)
    {
        if (topology->edges[edgeIndex].ax == ax &&
            topology->edges[edgeIndex].ay == ay &&
            topology->edges[edgeIndex].bx == bx &&
            topology->edges[edgeIndex].by == by)
        {
            return edgeIndex;
        }
    }

    return -1;
}

static int get_adjacent_tiles_for_node(const struct Topology *topology, int nodeIndex, int tileIds[3])
{
    int count = 0;
    const struct NodeRef *node = &topology->nodes[nodeIndex];

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        const Vector2 center = axial_to_world(kLandCoords[tileId], kBoardOrigin, kBoardRadius);
        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            int x = 0;
            int y = 0;
            get_corner_key(center, kBoardRadius, cornerIndex, &x, &y);
            if (corner_keys_match(node->x, node->y, x, y))
            {
                tileIds[count++] = tileId;
                break;
            }
        }
    }

    return count;
}

static void place_structure_on_node(struct Map *map, const struct Topology *topology, int nodeIndex, enum PlayerType owner, enum StructureType structure)
{
    const struct NodeRef *node = &topology->nodes[nodeIndex];

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        const Vector2 center = axial_to_world(kLandCoords[tileId], kBoardOrigin, kBoardRadius);
        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            int x = 0;
            int y = 0;
            get_corner_key(center, kBoardRadius, cornerIndex, &x, &y);
            if (!corner_keys_match(node->x, node->y, x, y))
            {
                continue;
            }

            map->tiles[tileId].corners[cornerIndex].owner = owner;
            map->tiles[tileId].corners[cornerIndex].structure = structure;
        }
    }
}

static void place_road_on_edge(struct Map *map, const struct Topology *topology, int edgeIndex, enum PlayerType owner)
{
    const struct EdgeRef *edge = &topology->edges[edgeIndex];

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        const Vector2 center = axial_to_world(kLandCoords[tileId], kBoardOrigin, kBoardRadius);
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            int ax = 0;
            int ay = 0;
            int bx = 0;
            int by = 0;
            get_edge_key(center, kBoardRadius, sideIndex, &ax, &ay, &bx, &by);
            if (ax != edge->ax || ay != edge->ay || bx != edge->bx || by != edge->by)
            {
                continue;
            }

            map->tiles[tileId].sides[sideIndex].isset = true;
            map->tiles[tileId].sides[sideIndex].player = owner;
        }
    }
}

static bool find_path_with_constraints(const struct Topology *topology, int desiredEdges,
                                       const bool forbiddenEdges[TOPOLOGY_MAX_EDGES],
                                       const bool forbiddenNodes[TOPOLOGY_MAX_NODES],
                                       struct PathRef *path)
{
    bool usedEdges[TOPOLOGY_MAX_EDGES] = {0};
    bool visitedNodes[TOPOLOGY_MAX_NODES] = {0};
    struct PathRef working;
    memset(&working, 0, sizeof(working));
    memset(path, 0, sizeof(*path));

    for (int startNode = 0; startNode < topology->nodeCount; startNode++)
    {
        if (forbiddenNodes != NULL && forbiddenNodes[startNode])
        {
            continue;
        }

        memset(usedEdges, 0, sizeof(usedEdges));
        memset(visitedNodes, 0, sizeof(visitedNodes));
        memset(&working, 0, sizeof(working));
        working.nodeIndices[0] = startNode;
        visitedNodes[startNode] = true;
        if (find_path_from_node(topology, startNode, desiredEdges, forbiddenEdges, forbiddenNodes,
                                usedEdges, visitedNodes, &working, 0, path))
        {
            return true;
        }
    }

    return false;
}

static bool find_path_from_node(const struct Topology *topology, int currentNode, int desiredEdges,
                                const bool forbiddenEdges[TOPOLOGY_MAX_EDGES],
                                const bool forbiddenNodes[TOPOLOGY_MAX_NODES],
                                bool usedEdges[TOPOLOGY_MAX_EDGES], bool visitedNodes[TOPOLOGY_MAX_NODES],
                                struct PathRef *working, int depth,
                                struct PathRef *result)
{
    if (depth == desiredEdges)
    {
        *result = *working;
        result->edgeCount = depth;
        return true;
    }

    for (int edgeIndex = 0; edgeIndex < topology->edgeCount; edgeIndex++)
    {
        int nextNode = -1;

        if (usedEdges[edgeIndex] || (forbiddenEdges != NULL && forbiddenEdges[edgeIndex]))
        {
            continue;
        }

        if (topology->edges[edgeIndex].nodeA == currentNode)
        {
            nextNode = topology->edges[edgeIndex].nodeB;
        }
        else if (topology->edges[edgeIndex].nodeB == currentNode)
        {
            nextNode = topology->edges[edgeIndex].nodeA;
        }
        else
        {
            continue;
        }

        if (forbiddenNodes != NULL && forbiddenNodes[nextNode])
        {
            continue;
        }
        if (visitedNodes[nextNode])
        {
            continue;
        }

        usedEdges[edgeIndex] = true;
        visitedNodes[nextNode] = true;
        working->edgeIndices[depth] = edgeIndex;
        working->nodeIndices[depth + 1] = nextNode;
        working->edgeCount = depth + 1;
        if (find_path_from_node(topology, nextNode, desiredEdges, forbiddenEdges, forbiddenNodes,
                                usedEdges, visitedNodes, working, depth + 1, result))
        {
            return true;
        }
        visitedNodes[nextNode] = false;
        usedEdges[edgeIndex] = false;
    }

    return false;
}

static void mark_path_edges(const struct PathRef *path, bool usedEdges[TOPOLOGY_MAX_EDGES])
{
    for (int edgeIndex = 0; edgeIndex < path->edgeCount; edgeIndex++)
    {
        usedEdges[path->edgeIndices[edgeIndex]] = true;
    }
}

static void apply_path_roads(struct Map *map, const struct Topology *topology, const struct PathRef *path, int roadCount, enum PlayerType owner)
{
    for (int i = 0; i < roadCount; i++)
    {
        place_road_on_edge(map, topology, path->edgeIndices[i], owner);
    }
}
