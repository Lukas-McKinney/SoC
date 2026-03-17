int gHoveredTileId = -1;
int gHoveredSideIndex = -1;
int gHoveredCornerTileId = -1;
int gHoveredCornerIndex = -1;
enum BuildMode gBuildMode = BUILD_MODE_NONE;
int gTradeGiveResource = RESOURCE_WOOD;
int gTradeReceiveResource = RESOURCE_WHEAT;
int gTradeAmount = 1;
int gPlayerTradeGiveResource = RESOURCE_WOOD;
int gPlayerTradeReceiveResource = RESOURCE_WHEAT;
enum PlayerType gPlayerTradeTarget = PLAYER_BLUE;
int gPlayerTradeGiveAmount = 1;
int gPlayerTradeReceiveAmount = 1;
int gDiscardSelection[5] = {0};
enum PlayerType gDiscardSelectionPlayer = PLAYER_NONE;
enum PlayerType gDiscardRevealPlayer = PLAYER_NONE;
enum PlayerType gThiefVictimRevealPlayer = PLAYER_NONE;
enum ResourceType gDevelopmentPlayPrimaryResource = RESOURCE_WOOD;
enum ResourceType gDevelopmentPlaySecondaryResource = RESOURCE_WHEAT;

#define UI_FONT_SPACING(fontSize) ((float)(fontSize) * 0.04f)

static bool IsMouseNearTile(Vector2 mouse, Vector2 center, float radius);
static float DistancePointToSegment(Vector2 p, Vector2 a, Vector2 b);
static void DrawBeachWedge(Vector2 center, float radius, int sideIndex);
static void DrawHarborMarker(const struct PortVisual *port, Vector2 center, float radius, int sideIndex);
static int FindPortSideIndex(const struct PortVisual *port);
static const char *PortLabel(const struct PortVisual *port);
static void DrawPipRow(Vector2 center, int pipCount, float spacing, float dotRadius, Color color);
Color PlayerColor(enum PlayerType player);
static bool EdgeKeysMatch(int ax1, int ay1, int bx1, int by1, int ax2, int ay2, int bx2, int by2);
static void CanonicalizeHoveredEdge(void);
static bool CornerKeysMatch(int x1, int y1, int x2, int y2);
static void CanonicalizeHoveredCorner(void);
static void NormalizeMaritimeTradeSelection(const struct Map *map);
static void NormalizePlayerTradeSelection(const struct Map *map);
static enum PlayerType LocalHumanPlayer(const struct Map *map);
static const char *PlayerNameLabel(enum PlayerType player);
static bool HasAnyValidRoadPlacement(const struct Map *map, Vector2 origin, float radius);
static bool CanOpenDevelopmentPlayOverlay(const struct Map *map, enum DevelopmentCardType type, Vector2 origin, float radius);

static void NormalizeMaritimeTradeSelection(const struct Map *map)
{
    int firstValidGiveResource = -1;
    int maxTradeAmount;

    if (map == NULL || map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
    {
        return;
    }

    for (int resource = 0; resource < 5; resource++)
    {
        if (map->players[map->currentPlayer].resources[resource] >= gameGetMaritimeTradeRate(map, (enum ResourceType)resource))
        {
            firstValidGiveResource = resource;
            break;
        }
    }

    if (firstValidGiveResource >= 0 &&
        map->players[map->currentPlayer].resources[gTradeGiveResource] < gameGetMaritimeTradeRate(map, (enum ResourceType)gTradeGiveResource))
    {
        gTradeGiveResource = firstValidGiveResource;
    }

    if (gTradeReceiveResource == gTradeGiveResource)
    {
        for (int resource = 0; resource < 5; resource++)
        {
            if (resource != gTradeGiveResource)
            {
                gTradeReceiveResource = resource;
                break;
            }
        }
    }

    maxTradeAmount = map->players[map->currentPlayer].resources[gTradeGiveResource] /
                     gameGetMaritimeTradeRate(map, (enum ResourceType)gTradeGiveResource);
    if (gTradeAmount > maxTradeAmount)
    {
        gTradeAmount = maxTradeAmount > 0 ? maxTradeAmount : 1;
    }
    if (gTradeAmount < 1)
    {
        gTradeAmount = 1;
    }
}

static void NormalizePlayerTradeSelection(const struct Map *map)
{
    if (map == NULL || map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
    {
        return;
    }

    if (gPlayerTradeTarget < PLAYER_RED || gPlayerTradeTarget > PLAYER_BLACK || gPlayerTradeTarget == map->currentPlayer)
    {
        for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
        {
            if (player != map->currentPlayer)
            {
                gPlayerTradeTarget = (enum PlayerType)player;
                break;
            }
        }
    }

    int firstGiveResource = -1;
    for (int resource = 0; resource < 5; resource++)
    {
        if (map->players[map->currentPlayer].resources[resource] > 0)
        {
            firstGiveResource = resource;
            break;
        }
    }

    if (firstGiveResource >= 0 && map->players[map->currentPlayer].resources[gPlayerTradeGiveResource] <= 0)
    {
        gPlayerTradeGiveResource = firstGiveResource;
    }

    if (gPlayerTradeReceiveResource == gPlayerTradeGiveResource)
    {
        for (int resource = 0; resource < 5; resource++)
        {
            if (resource != gPlayerTradeGiveResource)
            {
                gPlayerTradeReceiveResource = resource;
                break;
            }
        }
    }

    if (map->players[map->currentPlayer].resources[gPlayerTradeGiveResource] > 0 &&
        gPlayerTradeGiveAmount > map->players[map->currentPlayer].resources[gPlayerTradeGiveResource])
    {
        gPlayerTradeGiveAmount = map->players[map->currentPlayer].resources[gPlayerTradeGiveResource];
    }
    if (gPlayerTradeReceiveAmount > 9)
    {
        gPlayerTradeReceiveAmount = 9;
    }
    if (gPlayerTradeGiveAmount < 1)
    {
        gPlayerTradeGiveAmount = 1;
    }
    if (gPlayerTradeReceiveAmount < 1)
    {
        gPlayerTradeReceiveAmount = 1;
    }
}

static enum PlayerType LocalHumanPlayer(const struct Map *map)
{
    const struct MatchSession *session = matchSessionGetActive();
    enum PlayerType humanPlayer = PLAYER_NONE;
    int humanCount = 0;
    int aiCount = 0;

    if (map == NULL)
    {
        return PLAYER_NONE;
    }

    if (session != NULL && session->localPlayer >= PLAYER_RED && session->localPlayer <= PLAYER_BLACK)
    {
        return session->localPlayer;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (map->players[player].controlMode == PLAYER_CONTROL_AI)
        {
            aiCount++;
        }
        else
        {
            humanPlayer = (enum PlayerType)player;
            humanCount++;
        }
    }

    return aiCount > 0 && humanCount == 1 ? humanPlayer : PLAYER_NONE;
}

static const char *PlayerNameLabel(enum PlayerType player)
{
    return locPlayerName(player);
}

static bool HasAnyValidRoadPlacement(const struct Map *map, Vector2 origin, float radius)
{
    if (map == NULL || map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
    {
        return false;
    }

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            if (!IsCanonicalSharedEdge(tileId, sideIndex) ||
                IsSharedEdgeOccupied(map, tileId, sideIndex))
            {
                continue;
            }

            if (boardIsValidRoadPlacement(map, tileId, sideIndex, map->currentPlayer, origin, radius))
            {
                return true;
            }
        }
    }

    return false;
}

static bool CanOpenDevelopmentPlayOverlay(const struct Map *map, enum DevelopmentCardType type, Vector2 origin, float radius)
{
    if (!gameCanPlayDevelopmentCard(map, type))
    {
        return false;
    }

    if (type == DEVELOPMENT_CARD_ROAD_BUILDING)
    {
        return HasAnyValidRoadPlacement(map, origin, radius);
    }

    return true;
}
