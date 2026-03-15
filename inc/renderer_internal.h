#ifndef RENDERER_INTERNAL_H
#define RENDERER_INTERNAL_H

#include "renderer.h"
#include "renderer_ui.h"

enum
{
    LAND_TILE_COUNT = 19,
    HEX_CORNERS = 6
};

#define BOARD_HEX_RADIUS 68.0f
#define BOARD_ORIGIN_X_FACTOR 0.42f
#define BOARD_ORIGIN_Y_FACTOR 0.46f

struct AxialCoord
{
    int q;
    int r;
};

struct PortVisual
{
    struct AxialCoord oceanCoord;
    struct AxialCoord landCoord;
    bool generic;
    enum ResourceType resource;
};

enum
{
    PORT_VISUAL_COUNT = 9
};

extern bool gAssetsLoaded;
extern const struct AxialCoord kLandCoords[LAND_TILE_COUNT];
extern const struct PortVisual kPorts[PORT_VISUAL_COUNT];
extern int gHoveredTileId;
extern int gHoveredSideIndex;
extern int gHoveredCornerTileId;
extern int gHoveredCornerIndex;

/* Converts an axial board coordinate into the world-space center of its hex. */
Vector2 AxialToWorld(struct AxialCoord coord, Vector2 origin, float radius);

/* Returns the two corner indices that bound a side on a pointy-top hex. */
void GetSideCornerIndices(int sideIndex, int *cornerA, int *cornerB);

/* Returns a single corner position for a hex centered at `center`. */
Vector2 PointOnHex(Vector2 center, float radius, int cornerIndex);

/* Shared vector helpers used by both board drawing and input hit-testing. */
Vector2 LerpVec2(Vector2 a, Vector2 b, float t);
Vector2 Vec2Add(Vector2 a, Vector2 b);
Vector2 Vec2Scale(Vector2 v, float s);
Vector2 Vec2NormalizeSafe(Vector2 v);

/* Canonical shared-edge helpers keep roads and previews from being drawn twice. */
bool IsCanonicalSharedEdge(int tileId, int sideIndex);
bool IsSharedEdgeOccupied(const struct Map *map, int tileId, int sideIndex);
void PlaceRoadOnSharedEdge(struct Map *map, int tileId, int sideIndex, enum PlayerType player);

/* Canonical shared-corner helpers keep structures synchronized across touching tiles. */
bool IsCanonicalSharedCorner(int tileId, int cornerIndex);
void PlaceSettlementOnSharedCorner(struct Map *map, int tileId, int cornerIndex, enum PlayerType player, enum StructureType structure);

/* Board-drawing primitives shared by the board pipeline and interaction previews. */
void DrawRoad(Vector2 center, float radius, int sideIndex, enum PlayerType player, bool placed, bool available, bool hovered, float popAmount);
void DrawStructure(Vector2 center, float radius, int cornerIndex, enum PlayerType player, enum StructureType structure, bool available, bool hovered, float popAmount);
void DrawPort(const struct PortVisual *port, Vector2 center, float radius);
void DrawNumberToken(int number, Vector2 center, float radius);
void DrawThief(Vector2 center, float radius);
void DrawTileHighlightBorder(Vector2 center, float radius, Color glowColor, Color borderColor, float intensity);
void DrawTileHighlightWash(Vector2 center, float radius, Color color, float intensity);

#endif
