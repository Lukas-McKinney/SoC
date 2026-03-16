#include "board_rules.h"
#include "game_logic.h"
#include "match_session.h"
#include "renderer_internal.h"
#include "renderer_tiles.h"
#include "ui_state.h"

#include <math.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>

bool gAssetsLoaded = false;
static Texture2D gTileTextures[TILE_DESERT + 1];
static Texture2D gOceanTexture;
static Font gUiFont;
static bool gUiFontLoaded = false;
static RenderTexture2D gUiLayerTexture = {0};
static bool gUiLayerTextureLoaded = false;
static int gUiLayerTextureWidth = 0;
static int gUiLayerTextureHeight = 0;

static void EnsureUiLayerRenderTexture(void);
static void DrawMapUiLayer(const struct Map *map);
static bool HasAxialCoord(const struct AxialCoord *coords, int count, struct AxialCoord target);
static float GetRoadPlacementPopAmount(Vector2 center, float radius, int sideIndex);
static float GetStructurePlacementPopAmount(Vector2 center, float radius, int cornerIndex);
static float ClampAnimation01(float value);
static float EaseOutBack(float t);
static int HexDistance(struct AxialCoord coord);
static bool GetBoardCreationTransform(struct AxialCoord coord, Vector2 origin, Vector2 finalCenter, float finalRadius, Vector2 *animatedCenter, float *animatedRadius);
static Font LoadUiFont(const char *fontPath);

#define UI_FONT_SPACING(fontSize) ((float)(fontSize) * 0.04f)
#define UI_FONT_ASCII_FIRST 32
#define UI_FONT_ASCII_LAST 126
#define UI_FONT_EXTRA_GLYPH_COUNT 7

static Font LoadUiFont(const char *fontPath)
{
    static const int kExtraCodepoints[UI_FONT_EXTRA_GLYPH_COUNT] = {
        0x00C4, /* Ä */
        0x00D6, /* Ö */
        0x00DC, /* Ü */
        0x00DF, /* ß */
        0x00E4, /* ä */
        0x00F6, /* ö */
        0x00FC  /* ü */
    };
    int codepoints[(UI_FONT_ASCII_LAST - UI_FONT_ASCII_FIRST + 1) + UI_FONT_EXTRA_GLYPH_COUNT];
    int index = 0;

    for (int codepoint = UI_FONT_ASCII_FIRST; codepoint <= UI_FONT_ASCII_LAST; codepoint++)
    {
        codepoints[index++] = codepoint;
    }

    for (int i = 0; i < UI_FONT_EXTRA_GLYPH_COUNT; i++)
    {
        codepoints[index++] = kExtraCodepoints[i];
    }

    return LoadFontEx(fontPath, 64, codepoints, index);
}

void LoadRendererAssets(void)
{
    if (gAssetsLoaded)
    {
        return;
    }

    for (int type = TILE_FARMLAND; type <= TILE_DESERT; type++)
    {
        gTileTextures[type] = BuildTerrainTexture((enum TileType)type, 384, 332);
    }

    gOceanTexture = BuildOceanTexture(384, 332);
    const char *fontPath = "C:/Windows/Fonts/georgia.ttf";
    if (FileExists(fontPath))
    {
        gUiFont = LoadUiFont(fontPath);
        if (gUiFont.texture.id != 0)
        {
            SetTextureFilter(gUiFont.texture, TEXTURE_FILTER_BILINEAR);
            gUiFontLoaded = true;
        }
    }
    EnsureUiLayerRenderTexture();
    gAssetsLoaded = true;
}

void UnloadRendererAssets(void)
{
    if (!gAssetsLoaded)
    {
        return;
    }

    for (int type = TILE_FARMLAND; type <= TILE_DESERT; type++)
    {
        UnloadTexture(gTileTextures[type]);
    }

    UnloadTexture(gOceanTexture);
    if (gUiFontLoaded)
    {
        UnloadFont(gUiFont);
        gUiFontLoaded = false;
    }
    if (gUiLayerTextureLoaded)
    {
        UnloadRenderTexture(gUiLayerTexture);
        gUiLayerTextureLoaded = false;
        gUiLayerTextureWidth = 0;
        gUiLayerTextureHeight = 0;
    }
    gAssetsLoaded = false;
}

Font RendererGetUiFont(void)
{
    return gUiFontLoaded ? gUiFont : GetFontDefault();
}

void DrawUiText(const char *text, float x, float y, int fontSize, Color color)
{
    DrawTextEx(RendererGetUiFont(), text, (Vector2){x, y}, (float)fontSize, UI_FONT_SPACING(fontSize), color);
}

int MeasureUiText(const char *text, int fontSize)
{
    return (int)MeasureTextEx(RendererGetUiFont(), text, (float)fontSize, UI_FONT_SPACING(fontSize)).x;
}

void DrawMap(const struct Map *map)
{
    if (!gAssetsLoaded || map == NULL)
    {
        return;
    }

    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const int recentRollValue = uiGetRecentRollHighlightValue();
    const float recentRollHighlightProgress = uiGetRecentRollHighlightProgress();
    const float uiFadeProgress = uiGetBoardUiFadeProgress();
    const bool boardCreationAnimating = uiIsBoardCreationAnimating();
    const bool showPlacementPreviews = matchSessionLocalControlsPlayer(matchSessionGetActive(), map->currentPlayer);

    struct AxialCoord oceanCoords[HEX_CORNERS * 6];
    int oceanCount = 0;
    const struct AxialCoord directions[HEX_CORNERS] = {
        {1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};

    for (int i = 0; i < LAND_TILE_COUNT; i++)
    {
        for (int dir = 0; dir < HEX_CORNERS; dir++)
        {
            struct AxialCoord candidate = {
                kLandCoords[i].q + directions[dir].q,
                kLandCoords[i].r + directions[dir].r};

            if (HasAxialCoord(kLandCoords, LAND_TILE_COUNT, candidate) ||
                HasAxialCoord(oceanCoords, oceanCount, candidate))
            {
                continue;
            }

            oceanCoords[oceanCount++] = candidate;
        }
    }

    for (int i = 0; i < oceanCount; i++)
    {
        Vector2 center = AxialToWorld(oceanCoords[i], origin, radius);
        float animatedRadius = radius;
        if (!GetBoardCreationTransform(oceanCoords[i], origin, center, radius, &center, &animatedRadius))
        {
            continue;
        }

        DrawPoly(center, HEX_CORNERS, animatedRadius * 1.02f, -30.0f, (Color){56, 136, 191, 255});
        DrawTexturedHex(gOceanTexture, center, animatedRadius * 1.02f, WHITE);
        DrawCircleGradient((int)center.x, (int)center.y, animatedRadius * 0.92f, Fade((Color){189, 233, 250, 255}, 0.20f), BLANK);
        DrawRing(center, animatedRadius * 0.56f, animatedRadius * 0.84f, 0.0f, 360.0f, 48, Fade((Color){14, 89, 149, 255}, 0.08f));
        DrawLineEx(
            (Vector2){center.x - animatedRadius * 0.46f, center.y - animatedRadius * 0.08f},
            (Vector2){center.x - animatedRadius * 0.12f, center.y - animatedRadius * 0.14f},
            3.0f,
            Fade((Color){233, 249, 255, 255}, 0.42f));
        DrawLineEx(
            (Vector2){center.x + animatedRadius * 0.06f, center.y + animatedRadius * 0.10f},
            (Vector2){center.x + animatedRadius * 0.38f, center.y + animatedRadius * 0.04f},
            3.0f,
            Fade((Color){233, 249, 255, 255}, 0.38f));
        DrawCircleV((Vector2){center.x - animatedRadius * 0.10f, center.y + animatedRadius * 0.18f}, animatedRadius * 0.05f, Fade((Color){220, 245, 255, 255}, 0.18f));
        DrawCircleV((Vector2){center.x + animatedRadius * 0.18f, center.y - animatedRadius * 0.18f}, animatedRadius * 0.04f, Fade((Color){220, 245, 255, 255}, 0.16f));

        for (int portIndex = 0; portIndex < PORT_VISUAL_COUNT; portIndex++)
        {
            if (kPorts[portIndex].oceanCoord.q == oceanCoords[i].q &&
                kPorts[portIndex].oceanCoord.r == oceanCoords[i].r)
            {
                DrawPort(&kPorts[portIndex], center, animatedRadius);
            }
        }

        DrawPolyLinesEx(center, HEX_CORNERS, animatedRadius, -30.0f, 3.0f, Fade((Color){19, 73, 122, 255}, 0.92f));
    }

    for (int i = 0; i < LAND_TILE_COUNT; i++)
    {
        const struct Tile *tile = &map->tiles[i];
        Vector2 center = AxialToWorld(kLandCoords[i], origin, radius);
        float animatedRadius = radius;
        if (!GetBoardCreationTransform(kLandCoords[i], origin, center, radius, &center, &animatedRadius))
        {
            continue;
        }

        DrawTileBaseBackground(tile->type, center, animatedRadius);
        DrawTexturedHex(gTileTextures[tile->type], center, animatedRadius, WHITE);
        DrawTileBackdrop(tile->type, center, animatedRadius * 0.82f);
        DrawTilePattern(tile->type, center, animatedRadius * 0.72f);
        DrawTileIllustration(tile->type, center, animatedRadius * 0.91f);

        DrawPolyLinesEx(center, HEX_CORNERS, animatedRadius, -30.0f, 4.0f, Fade((Color){88, 57, 24, 255}, 0.95f));

        if (recentRollHighlightProgress > 0.0f &&
            tile->diceNumber == recentRollValue &&
            tile->type != TILE_DESERT &&
            map->thiefTileId != tile->id)
        {
            DrawTileHighlightBorder(center, animatedRadius, (Color){177, 219, 108, 255}, (Color){74, 134, 56, 255}, recentRollHighlightProgress);
        }

        if (map->thiefTileId == tile->id && tile->type != TILE_DESERT)
        {
            DrawTileHighlightWash(center, animatedRadius, (Color){238, 92, 81, 255}, 0.88f);
            DrawTileHighlightBorder(center, animatedRadius, (Color){221, 78, 67, 255}, (Color){132, 32, 28, 255}, 0.80f);
        }

        if (tile->diceNumber > 0)
        {
            DrawNumberToken(tile->diceNumber, center, animatedRadius);
        }

        if (map->thiefTileId == tile->id)
        {
            DrawThief(center, animatedRadius);
        }

        if (gameCanMoveThiefToTile(map, tile->id))
        {
            const bool hovered = tile->id == gHoveredTileId;
            DrawPolyLinesEx(center, HEX_CORNERS, animatedRadius * 0.93f, -30.0f, hovered ? 5.0f : 3.0f, hovered ? (Color){236, 194, 65, 255} : Fade((Color){236, 194, 65, 255}, 0.55f));
            DrawPoly(center, HEX_CORNERS, animatedRadius * 0.94f, -30.0f, hovered ? Fade((Color){247, 220, 119, 255}, 0.16f) : Fade((Color){247, 220, 119, 255}, 0.07f));
        }
    }

    if (!boardCreationAnimating)
    {
        for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
        {
            const struct Tile *tile = &map->tiles[tileId];
            Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
            for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
            {
                if (tile->sides[sideIndex].isset && IsCanonicalSharedEdge(tileId, sideIndex))
                {
                    DrawRoad(center, radius, sideIndex, tile->sides[sideIndex].player, true, false, false, GetRoadPlacementPopAmount(center, radius, sideIndex));
                }
            }
        }

        if (showPlacementPreviews && gBuildMode == BUILD_MODE_ROAD)
        {
            for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
            {
                Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
                for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
                {
                    if (!IsCanonicalSharedEdge(tileId, sideIndex) ||
                        IsSharedEdgeOccupied(map, tileId, sideIndex) ||
                        !boardIsValidRoadPlacement(map, tileId, sideIndex, map->currentPlayer, origin, radius) ||
                        (map->phase == GAME_PHASE_SETUP &&
                         !boardEdgeTouchesCorner(tileId, sideIndex, map->setupSettlementTileId, map->setupSettlementCornerIndex, origin, radius)))
                    {
                        continue;
                    }

                    const bool hovered = tileId == gHoveredTileId && sideIndex == gHoveredSideIndex;
                    DrawRoad(center, radius, sideIndex, map->currentPlayer, false, true, hovered, 0.0f);
                }
            }
        }

        for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
        {
            const struct Tile *tile = &map->tiles[tileId];
            Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
            for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
            {
                if (tile->corners[cornerIndex].structure != STRUCTURE_NONE && IsCanonicalSharedCorner(tileId, cornerIndex))
                {
                    DrawStructure(center, radius, cornerIndex, tile->corners[cornerIndex].owner, tile->corners[cornerIndex].structure, false, false, GetStructurePlacementPopAmount(center, radius, cornerIndex));
                }
            }
        }

        if (showPlacementPreviews && (gBuildMode == BUILD_MODE_SETTLEMENT || gBuildMode == BUILD_MODE_CITY))
        {
            for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
            {
                Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
                for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
                {
                    if (!IsCanonicalSharedCorner(tileId, cornerIndex))
                    {
                        continue;
                    }

                    bool valid = false;
                    enum StructureType previewStructure = STRUCTURE_TOWN;
                    if (gBuildMode == BUILD_MODE_SETTLEMENT)
                    {
                        valid = boardIsValidSettlementPlacement(map, tileId, cornerIndex, map->currentPlayer, origin, radius);
                        previewStructure = STRUCTURE_TOWN;
                    }
                    else
                    {
                        valid = boardIsValidCityPlacement(map, tileId, cornerIndex, map->currentPlayer);
                        previewStructure = STRUCTURE_CITY;
                    }

                    if (!valid)
                    {
                        continue;
                    }

                    const bool hovered = tileId == gHoveredCornerTileId && cornerIndex == gHoveredCornerIndex;
                    DrawStructure(center, radius, cornerIndex, map->currentPlayer, previewStructure, true, hovered, 0.0f);
                }
            }
        }
    }

    if (uiFadeProgress > 0.0f)
    {
        if (uiFadeProgress >= 0.995f)
        {
            DrawMapUiLayer(map);
        }
        else
        {
            EnsureUiLayerRenderTexture();
            if (gUiLayerTextureLoaded)
            {
                BeginTextureMode(gUiLayerTexture);
                ClearBackground(BLANK);
                DrawMapUiLayer(map);
                EndTextureMode();

                DrawTexturePro(
                    gUiLayerTexture.texture,
                    (Rectangle){0.0f, 0.0f, (float)gUiLayerTexture.texture.width, -(float)gUiLayerTexture.texture.height},
                    (Rectangle){0.0f, 0.0f, (float)gUiLayerTextureWidth, (float)gUiLayerTextureHeight},
                    (Vector2){0.0f, 0.0f},
                    0.0f,
                    Fade(WHITE, uiFadeProgress));
            }
            else
            {
                DrawMapUiLayer(map);
            }
        }
    }
}

static void EnsureUiLayerRenderTexture(void)
{
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();

    if (gUiLayerTextureLoaded &&
        gUiLayerTextureWidth == screenWidth &&
        gUiLayerTextureHeight == screenHeight)
    {
        return;
    }

    if (gUiLayerTextureLoaded)
    {
        UnloadRenderTexture(gUiLayerTexture);
        gUiLayerTextureLoaded = false;
    }

    gUiLayerTexture = LoadRenderTexture(screenWidth, screenHeight);
    gUiLayerTextureLoaded = gUiLayerTexture.texture.id != 0;
    gUiLayerTextureWidth = screenWidth;
    gUiLayerTextureHeight = screenHeight;
    if (gUiLayerTextureLoaded)
    {
        SetTextureFilter(gUiLayerTexture.texture, TEXTURE_FILTER_BILINEAR);
    }
}

static void DrawMapUiLayer(const struct Map *map)
{
    DrawOpponentVictoryBar(map);
    DrawAwardCards(map);
    DrawPlayerPanel(map);
    DrawDevelopmentHand(map);
    DrawTurnPanel(map);
    DrawSettingsButton();
    if (!gameHasWinner(map))
    {
        DrawTradeButton(map);
        DrawPlayerTradeButton(map);
        DrawBuildPanel(map);
        if (uiGetDevelopmentPurchaseConfirmOpenAmount() > 0.01f)
        {
            DrawDevelopmentPurchaseOverlay(map);
        }
        if (uiGetDevelopmentPlayConfirmOpenAmount() > 0.01f)
        {
            DrawDevelopmentPlayOverlay(map);
        }
        if (uiGetTradeMenuOpenAmount() > 0.01f)
        {
            DrawTradeModal(map);
        }
        if (uiGetPlayerTradeMenuOpenAmount() > 0.01f)
        {
            DrawPlayerTradeModal(map);
        }
        if (matchSessionHasPendingTradeOfferForLocalResponse(matchSessionGetActive()))
        {
            DrawIncomingTradeOfferModal(map);
        }
        if (gameHasPendingDiscards(map))
        {
            DrawDiscardModal(map);
        }
        else if (gameNeedsThiefVictimSelection(map))
        {
            DrawThiefVictimModal(map);
        }
    }
    if (uiIsDevelopmentCardDrawAnimating())
    {
        DrawDevelopmentCardDrawAnimation(map);
    }
    if (uiHasCenteredStatus())
    {
        DrawCenteredStatus();
    }
    if (uiHasCenteredWarning())
    {
        DrawCenteredWarning();
    }
    if (uiGetSettingsMenuOpenAmount() > 0.01f)
    {
        DrawSettingsModal();
    }
    if (gameHasWinner(map))
    {
        DrawVictoryOverlay(map);
    }
}

static bool HasAxialCoord(const struct AxialCoord *coords, int count, struct AxialCoord target)
{
    for (int i = 0; i < count; i++)
    {
        if (coords[i].q == target.q && coords[i].r == target.r)
        {
            return true;
        }
    }

    return false;
}

static float GetRoadPlacementPopAmount(Vector2 center, float radius, int sideIndex)
{
    int ax = 0;
    int ay = 0;
    int bx = 0;
    int by = 0;
    RendererGetRoadEdgeKey(center, radius, sideIndex, &ax, &ay, &bx, &by);
    return uiGetRoadPlacementPopAmount(ax, ay, bx, by);
}

static float GetStructurePlacementPopAmount(Vector2 center, float radius, int cornerIndex)
{
    int x = 0;
    int y = 0;
    RendererGetCornerKey(center, radius, cornerIndex, &x, &y);
    return uiGetStructurePlacementPopAmount(x, y);
}

static float ClampAnimation01(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }
    if (value > 1.0f)
    {
        return 1.0f;
    }
    return value;
}

static float EaseOutBack(float t)
{
    const float overshoot = 1.70158f;
    const float scale = overshoot + 1.0f;
    const float x = t - 1.0f;
    return 1.0f + scale * x * x * x + overshoot * x * x;
}

static int HexDistance(struct AxialCoord coord)
{
    const int q = coord.q < 0 ? -coord.q : coord.q;
    const int r = coord.r < 0 ? -coord.r : coord.r;
    const int sValue = -coord.q - coord.r;
    const int s = sValue < 0 ? -sValue : sValue;
    int distance = q;
    if (r > distance)
    {
        distance = r;
    }
    if (s > distance)
    {
        distance = s;
    }
    return distance;
}

static bool GetBoardCreationTransform(struct AxialCoord coord, Vector2 origin, Vector2 finalCenter, float finalRadius, Vector2 *animatedCenter, float *animatedRadius)
{
    const float globalTime = uiGetBoardCreationAnimationProgress() * 1.15f;
    const int distance = HexDistance(coord);
    const float delay = distance * 0.14f + (distance >= 3 ? 0.06f : 0.0f);
    const float duration = 0.34f;

    if (animatedCenter == NULL || animatedRadius == NULL)
    {
        return false;
    }

    if (globalTime <= delay)
    {
        return false;
    }

    {
        const float progress = ClampAnimation01((globalTime - delay) / duration);
        const float eased = EaseOutBack(progress);
        const Vector2 startCenter = {
            origin.x + (finalCenter.x - origin.x) * 0.16f,
            origin.y + (finalCenter.y - origin.y) * 0.16f - 34.0f - distance * 18.0f};
        *animatedCenter = LerpVec2(startCenter, finalCenter, eased);
        *animatedRadius = finalRadius * (0.36f + 0.64f * eased);
        return true;
    }
}
