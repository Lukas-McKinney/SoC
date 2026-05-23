#include "board_rules.h"
#include "debug_log.h"

#include <stddef.h>
#include <math.h>

#define LAND_TILE_COUNT 19
#define HEX_CORNERS 6

struct AxialCoord
{
    int q;
    int r;
};

static const struct AxialCoord kLandCoords[LAND_TILE_COUNT] = {
    {0, -2}, {1, -2}, {2, -2}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}, {-2, 2}, {-1, 2}, {0, 2}};

static Vector2 axial_to_world(struct AxialCoord coord, Vector2 origin, float radius);
static void get_side_corner_indices(int sideIndex, int *cornerA, int *cornerB);
static Vector2 point_on_hex(Vector2 center, float radius, int cornerIndex);
static void get_road_edge_key(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by);
static void get_corner_key(Vector2 center, float radius, int cornerIndex, int *x, int *y);
static bool corner_keys_match(int x1, int y1, int x2, int y2);
static bool is_shared_corner_occupied(const struct Map *map, int tileId, int cornerIndex, Vector2 origin, float radius);
static bool has_adjacent_settlement(const struct Map *map, int tileId, int cornerIndex, Vector2 origin, float radius);
static int count_player_roads_touching_corner(const struct Map *map, enum PlayerType player, int tileId, int cornerIndex, Vector2 origin, float radius);
static bool corner_touches_owned_road(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player, Vector2 origin, float radius);
static enum PlayerType get_shared_corner_owner_by_key(const struct Map *map, int x, int y, Vector2 origin, float radius);
static bool player_has_road_at_corner_key(const struct Map *map, enum PlayerType player, int x, int y, Vector2 origin, float radius);
static const char *settlement_placement_failure_reason(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player, Vector2 origin, float radius);
static const char *city_placement_failure_reason(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player);
static bool is_canonical_shared_corner(int tileId, int cornerIndex, Vector2 origin, float radius);

bool boardEdgeTouchesCorner(int edgeTileId, int sideIndex, int cornerTileId, int cornerIndex, Vector2 origin, float radius)
{
    if (edgeTileId < 0 || sideIndex < 0 || cornerTileId < 0 || cornerIndex < 0)
    {
        return false;
    }

    const Vector2 edgeCenter = axial_to_world(kLandCoords[edgeTileId], origin, radius);
    const Vector2 cornerCenter = axial_to_world(kLandCoords[cornerTileId], origin, radius);
    int cx = 0;
    int cy = 0;
    get_corner_key(cornerCenter, radius, cornerIndex, &cx, &cy);

    int aIndex = 0;
    int bIndex = 1;
    get_side_corner_indices(sideIndex, &aIndex, &bIndex);
    int ax = 0;
    int ay = 0;
    int bx = 0;
    int by = 0;
    get_corner_key(edgeCenter, radius, aIndex, &ax, &ay);
    get_corner_key(edgeCenter, radius, bIndex, &bx, &by);

    return corner_keys_match(ax, ay, cx, cy) || corner_keys_match(bx, by, cx, cy);
}

bool boardIsValidRoadPlacement(const struct Map *map, int tileId, int sideIndex, enum PlayerType player, Vector2 origin, float radius)
{
    if (map->phase == GAME_PHASE_SETUP)
    {
        return true;
    }

    const Vector2 center = axial_to_world(kLandCoords[tileId], origin, radius);
    int ax = 0;
    int ay = 0;
    int bx = 0;
    int by = 0;
    get_road_edge_key(center, radius, sideIndex, &ax, &ay, &bx, &by);

    const int endpointX[2] = {ax, bx};
    const int endpointY[2] = {ay, by};
    for (int i = 0; i < 2; i++)
    {
        const enum PlayerType owner = get_shared_corner_owner_by_key(map, endpointX[i], endpointY[i], origin, radius);

        if (owner == player)
        {
            return true;
        }

        if (owner != PLAYER_NONE && owner != player)
        {
            continue;
        }

        if (player_has_road_at_corner_key(map, player, endpointX[i], endpointY[i], origin, radius))
        {
            return true;
        }
    }

    return false;
}

/* Count how many settlements a player has built on the board */
static int count_player_settlements(const struct Map *map, enum PlayerType player)
{
    int count = 0;
    const Vector2 origin = {(float)GetScreenWidth() * 0.42f, (float)GetScreenHeight() * 0.46f};
    const float radius = 68.0f;
    for (int i = 0; i < LAND_TILE_COUNT; i++)
    {
        for (int j = 0; j < HEX_CORNERS; j++)
        {
            if (map->tiles[i].corners[j].owner == player &&
                map->tiles[i].corners[j].structure == STRUCTURE_TOWN &&
                is_canonical_shared_corner(i, j, origin, radius))
            {
                count++;
            }
        }
    }
    return count;
}

/* Count how many cities a player has built on the board */
static int count_player_cities(const struct Map *map, enum PlayerType player)
{
    int count = 0;
    const Vector2 origin = {(float)GetScreenWidth() * 0.42f, (float)GetScreenHeight() * 0.46f};
    const float radius = 68.0f;
    for (int i = 0; i < LAND_TILE_COUNT; i++)
    {
        for (int j = 0; j < HEX_CORNERS; j++)
        {
            if (map->tiles[i].corners[j].owner == player &&
                map->tiles[i].corners[j].structure == STRUCTURE_CITY &&
                is_canonical_shared_corner(i, j, origin, radius))
            {
                count++;
            }
        }
    }
    return count;
}

bool boardIsValidSettlementPlacement(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player, Vector2 origin, float radius)
{
    return settlement_placement_failure_reason(map, tileId, cornerIndex, player, origin, radius) == NULL;
}

const char *boardGetSettlementPlacementFailureReason(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player, Vector2 origin, float radius)
{
    return settlement_placement_failure_reason(map, tileId, cornerIndex, player, origin, radius);
}

bool boardIsValidCityPlacement(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player)
{
    return city_placement_failure_reason(map, tileId, cornerIndex, player) == NULL;
}

const char *boardGetCityPlacementFailureReason(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player)
{
    return city_placement_failure_reason(map, tileId, cornerIndex, player);
}

static Vector2 axial_to_world(struct AxialCoord coord, Vector2 origin, float radius)
{
    const float root3 = 1.7320508f;
    return (Vector2){
        origin.x + radius * root3 * ((float)coord.q + (float)coord.r * 0.5f),
        origin.y + radius * 1.5f * (float)coord.r};
}

static void get_side_corner_indices(int sideIndex, int *cornerA, int *cornerB)
{
    static const int sideCorners[HEX_CORNERS][2] = {
        {0, 1}, {5, 0}, {4, 5}, {3, 4}, {2, 3}, {1, 2}};
    const int index = ((sideIndex % HEX_CORNERS) + HEX_CORNERS) % HEX_CORNERS;
    *cornerA = sideCorners[index][0];
    *cornerB = sideCorners[index][1];
}

static Vector2 point_on_hex(Vector2 center, float radius, int cornerIndex)
{
    const float angle = DEG2RAD * (60.0f * (float)cornerIndex - 30.0f);
    return (Vector2){
        center.x + cosf(angle) * radius,
        center.y + sinf(angle) * radius};
}

static void get_road_edge_key(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by)
{
    int aIndex = 0;
    int bIndex = 1;
    get_side_corner_indices(sideIndex, &aIndex, &bIndex);
    const Vector2 a = point_on_hex(center, radius, aIndex);
    const Vector2 b = point_on_hex(center, radius, bIndex);

    int x1 = (int)roundf(a.x * 10.0f);
    int y1 = (int)roundf(a.y * 10.0f);
    int x2 = (int)roundf(b.x * 10.0f);
    int y2 = (int)roundf(b.y * 10.0f);

    if (x1 > x2 || (x1 == x2 && y1 > y2))
    {
        int tx = x1;
        x1 = x2;
        x2 = tx;
        int ty = y1;
        y1 = y2;
        y2 = ty;
    }

    *ax = x1;
    *ay = y1;
    *bx = x2;
    *by = y2;
}

static void get_corner_key(Vector2 center, float radius, int cornerIndex, int *x, int *y)
{
    const Vector2 point = point_on_hex(center, radius, cornerIndex);
    *x = (int)roundf(point.x * 10.0f);
    *y = (int)roundf(point.y * 10.0f);
}

static bool corner_keys_match(int x1, int y1, int x2, int y2)
{
    return x1 == x2 && y1 == y2;
}

static bool is_shared_corner_occupied(const struct Map *map, int tileId, int cornerIndex, Vector2 origin, float radius)
{
    const Vector2 center = axial_to_world(kLandCoords[tileId], origin, radius);
    int tx = 0;
    int ty = 0;
    get_corner_key(center, radius, cornerIndex, &tx, &ty);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int ox = 0;
            int oy = 0;
            get_corner_key(otherCenter, radius, otherCorner, &ox, &oy);
            if (corner_keys_match(tx, ty, ox, oy) &&
                map->tiles[otherTile].corners[otherCorner].structure != STRUCTURE_NONE)
            {
                return true;
            }
        }
    }

    return false;
}

static bool has_adjacent_settlement(const struct Map *map, int tileId, int cornerIndex, Vector2 origin, float radius)
{
    const Vector2 center = axial_to_world(kLandCoords[tileId], origin, radius);
    const Vector2 target = point_on_hex(center, radius, cornerIndex);
    int tx = 0;
    int ty = 0;
    get_corner_key(center, radius, cornerIndex, &tx, &ty);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            if (map->tiles[otherTile].corners[otherCorner].structure == STRUCTURE_NONE)
            {
                continue;
            }

            int ox = 0;
            int oy = 0;
            get_corner_key(otherCenter, radius, otherCorner, &ox, &oy);
            if (corner_keys_match(tx, ty, ox, oy))
            {
                continue;
            }

            const Vector2 other = point_on_hex(otherCenter, radius, otherCorner);
            const float dx = target.x - other.x;
            const float dy = target.y - other.y;
            const float distance = sqrtf(dx * dx + dy * dy);
            if (distance < radius * 1.05f)
            {
                return true;
            }
        }
    }

    return false;
}

static int count_player_roads_touching_corner(const struct Map *map, enum PlayerType player, int tileId, int cornerIndex, Vector2 origin, float radius)
{
    int count = 0;
    const Vector2 cornerCenter = axial_to_world(kLandCoords[tileId], origin, radius);
    int cx = 0;
    int cy = 0;
    get_corner_key(cornerCenter, radius, cornerIndex, &cx, &cy);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
        for (int otherSide = 0; otherSide < HEX_CORNERS; otherSide++)
        {
            if (!map->tiles[otherTile].sides[otherSide].isset ||
                map->tiles[otherTile].sides[otherSide].player != player)
            {
                continue;
            }

            int ax = 0;
            int ay = 0;
            int bx = 0;
            int by = 0;
            get_road_edge_key(otherCenter, radius, otherSide, &ax, &ay, &bx, &by);
            if (corner_keys_match(cx, cy, ax, ay) || corner_keys_match(cx, cy, bx, by))
            {
                count++;
            }
        }
    }

    return count;
}

static bool corner_touches_owned_road(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player, Vector2 origin, float radius)
{
    const Vector2 cornerCenter = axial_to_world(kLandCoords[tileId], origin, radius);
    int cx = 0;
    int cy = 0;
    get_corner_key(cornerCenter, radius, cornerIndex, &cx, &cy);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
        for (int otherSide = 0; otherSide < HEX_CORNERS; otherSide++)
        {
            if (!map->tiles[otherTile].sides[otherSide].isset ||
                map->tiles[otherTile].sides[otherSide].player != player)
            {
                continue;
            }

            int ax = 0;
            int ay = 0;
            int bx = 0;
            int by = 0;
            get_road_edge_key(otherCenter, radius, otherSide, &ax, &ay, &bx, &by);
            if (corner_keys_match(cx, cy, ax, ay) || corner_keys_match(cx, cy, bx, by))
            {
                return true;
            }
        }
    }

    return false;
}

static enum PlayerType get_shared_corner_owner_by_key(const struct Map *map, int x, int y, Vector2 origin, float radius)
{
    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int ox = 0;
            int oy = 0;
            get_corner_key(otherCenter, radius, otherCorner, &ox, &oy);
            if (corner_keys_match(x, y, ox, oy) &&
                map->tiles[otherTile].corners[otherCorner].owner != PLAYER_NONE)
            {
                return map->tiles[otherTile].corners[otherCorner].owner;
            }
        }
    }

    return PLAYER_NONE;
}

static bool player_has_road_at_corner_key(const struct Map *map, enum PlayerType player, int x, int y, Vector2 origin, float radius)
{
    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
        for (int otherSide = 0; otherSide < HEX_CORNERS; otherSide++)
        {
            if (!map->tiles[otherTile].sides[otherSide].isset ||
                map->tiles[otherTile].sides[otherSide].player != player)
            {
                continue;
            }

            int ax = 0;
            int ay = 0;
            int bx = 0;
            int by = 0;
            get_road_edge_key(otherCenter, radius, otherSide, &ax, &ay, &bx, &by);
            if (corner_keys_match(x, y, ax, ay) || corner_keys_match(x, y, bx, by))
            {
                return true;
            }
        }
    }

    return false;
}

static bool is_canonical_shared_corner(int tileId, int cornerIndex, Vector2 origin, float radius)
{
    const Vector2 center = axial_to_world(kLandCoords[tileId], origin, radius);
    int tx = 0;
    int ty = 0;
    get_corner_key(center, radius, cornerIndex, &tx, &ty);

    for (int otherTile = 0; otherTile < tileId; otherTile++)
    {
        const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int ox = 0;
            int oy = 0;
            get_corner_key(otherCenter, radius, otherCorner, &ox, &oy);
            if (corner_keys_match(tx, ty, ox, oy))
            {
                return false;
            }
        }
    }

    return true;
}

static const char *city_placement_failure_reason(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player)
{
    if (map == NULL)
    {
        return "map is null";
    }

    if (tileId < 0 || tileId >= LAND_TILE_COUNT || cornerIndex < 0 || cornerIndex >= HEX_CORNERS)
    {
        return "invalid tile or corner";
    }

    if (map->phase != GAME_PHASE_PLAY)
    {
        return "cities can only be placed in play phase";
    }

    if (count_player_cities(map, player) >= 4)
    {
        return "city limit reached";
    }

    const struct Corner *corner = &map->tiles[tileId].corners[cornerIndex];
    if (corner->owner != player || corner->structure != STRUCTURE_TOWN)
    {
        return "you do not own a settlement here";
    }

    return NULL;
}

static const char *settlement_placement_failure_reason(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player, Vector2 origin, float radius)
{
    if (map == NULL)
    {
        return "map is null";
    }

    if (tileId < 0 || cornerIndex < 0)
    {
        return "invalid tile or corner";
    }

    if (count_player_settlements(map, player) >= 5)
    {
        return "settlement limit reached";
    }

    if (is_shared_corner_occupied(map, tileId, cornerIndex, origin, radius))
    {
        return "corner already occupied";
    }

    if (has_adjacent_settlement(map, tileId, cornerIndex, origin, radius))
    {
        return "adjacent settlement too close";
    }

    if (map->phase == GAME_PHASE_SETUP)
    {
        return map->setupNeedsRoad ? "setup settlement is waiting for a road" : NULL;
    }

    for (enum PlayerType otherPlayer = PLAYER_RED; otherPlayer <= PLAYER_BLACK; otherPlayer++)
    {
        if (otherPlayer == player)
        {
            continue;
        }

        if (count_player_roads_touching_corner(map, otherPlayer, tileId, cornerIndex, origin, radius) >= 2)
        {
            return "corner blocks another player's road";
        }
    }

    if (!corner_touches_owned_road(map, tileId, cornerIndex, player, origin, radius))
    {
        return "no owned road touches this corner";
    }

    return NULL;
}
