#include "renderer_tiles.h"

#include <math.h>
#include <raylib.h>
#include <rlgl.h>

#define HEX_CORNERS 6

static void BuildHexPoints(Vector2 center, float radius, Vector2 points[HEX_CORNERS]);
static float HexHalfWidthAtYOffset(float radius, float yOffset);
static Color TileBaseColor(enum TileType type);
static Color TileAccentColor(enum TileType type);

static void BuildHexPoints(Vector2 center, float radius, Vector2 points[HEX_CORNERS])
{
    for (int i = 0; i < HEX_CORNERS; i++)
    {
        const float angle = DEG2RAD * (60.0f * (float)i - 30.0f);
        points[i] = (Vector2){
            center.x + cosf(angle) * radius,
            center.y + sinf(angle) * radius};
    }
}

Texture2D BuildTerrainTexture(enum TileType type, int width, int height)
{
    Image image = GenImageColor(width, height, TileBaseColor(type));
    const Color accent = TileAccentColor(type);
    const Color lowlight = ColorBrightness(accent, -0.22f);
    const Color highlight = ColorBrightness(accent, 0.24f);

    switch (type)
    {
    case TILE_FARMLAND:
        for (int y = 0; y < height; y += 20)
        {
            ImageDrawLineEx(&image, (Vector2){0.0f, (float)y}, (Vector2){(float)width, (float)(y + 16)}, 12, Fade((Color){188, 150, 41, 255}, 0.42f));
        }
        for (int y = 12; y < height; y += 26)
        {
            for (int x = 10 + ((y / 26) % 2) * 10; x < width; x += 18)
            {
                ImageDrawLineEx(&image, (Vector2){(float)x, (float)(y + 10)}, (Vector2){(float)(x - 1), (float)(y - 7)}, 2, Fade((Color){130, 101, 20, 255}, 0.72f));
                ImageDrawLineEx(&image, (Vector2){(float)x, (float)(y - 4)}, (Vector2){(float)(x - 5), (float)(y - 9)}, 2, Fade(highlight, 0.88f));
                ImageDrawLineEx(&image, (Vector2){(float)x, (float)(y - 1)}, (Vector2){(float)(x + 5), (float)(y - 6)}, 2, Fade(highlight, 0.88f));
                ImageDrawLineEx(&image, (Vector2){(float)x, (float)(y + 2)}, (Vector2){(float)(x - 5), (float)(y - 3)}, 2, Fade(highlight, 0.82f));
                ImageDrawLineEx(&image, (Vector2){(float)x, (float)(y + 5)}, (Vector2){(float)(x + 5), (float)(y + 0)}, 2, Fade(highlight, 0.82f));
            }
        }
        break;
    case TILE_SHEEPMEADOW:
        for (int y = 0; y < height; y += 42)
        {
            ImageDrawCircle(&image, width / 4, y + 28, 48, Fade((Color){89, 152, 57, 255}, 0.40f));
            ImageDrawCircle(&image, (width * 3) / 4, y + 34, 60, Fade((Color){97, 166, 64, 255}, 0.36f));
        }
        for (int y = 18; y < height; y += 46)
        {
            for (int x = 18 + ((y / 46) % 2) * 22; x < width; x += 62)
            {
                ImageDrawCircle(&image, x, y, 10, Fade(RAYWHITE, 0.98f));
                ImageDrawCircle(&image, x - 8, y + 4, 8, Fade(RAYWHITE, 0.96f));
                ImageDrawCircle(&image, x + 8, y + 4, 8, Fade(RAYWHITE, 0.96f));
                ImageDrawCircle(&image, x + 13, y + 2, 4, Fade((Color){64, 54, 44, 255}, 0.92f));
                ImageDrawLineEx(&image, (Vector2){(float)(x - 5), (float)(y + 10)}, (Vector2){(float)(x - 7), (float)(y + 16)}, 2, Fade((Color){64, 54, 44, 255}, 0.72f));
                ImageDrawLineEx(&image, (Vector2){(float)(x + 4), (float)(y + 10)}, (Vector2){(float)(x + 2), (float)(y + 16)}, 2, Fade((Color){64, 54, 44, 255}, 0.72f));
            }
        }
        break;
    case TILE_MINE:
        for (int y = 24; y < height + 24; y += 56)
        {
            for (int x = -8; x < width + 28; x += 58)
            {
                Vector2 a = {(float)x, (float)(y + 22)};
                Vector2 b = {(float)(x + 18), (float)(y - 14)};
                Vector2 c = {(float)(x + 38), (float)(y + 22)};
                Vector2 d = {(float)(x + 18), (float)(y + 40)};
                ImageDrawTriangle(&image, a, b, c, Fade(accent, 0.80f));
                ImageDrawTriangle(&image, a, c, d, Fade(lowlight, 0.95f));
                ImageDrawTriangle(&image, b, (Vector2){(float)(x + 10), (float)(y + 2)}, (Vector2){(float)(x + 24), (float)(y + 2)}, Fade(RAYWHITE, 0.75f));
                ImageDrawLineEx(&image, (Vector2){(float)(x + 6), (float)(y + 24)}, (Vector2){(float)(x + 32), (float)(y + 24)}, 3, Fade((Color){111, 67, 36, 255}, 0.50f));
            }
        }
        break;
    case TILE_FOREST:
        for (int y = 18; y < height + 22; y += 40)
        {
            for (int x = 16 + ((y / 40) % 2) * 16; x < width + 16; x += 46)
            {
                ImageDrawCircle(&image, x, y, 14, Fade((Color){33, 96, 45, 255}, 0.96f));
                ImageDrawCircle(&image, x - 10, y + 6, 11, Fade(accent, 0.92f));
                ImageDrawCircle(&image, x + 10, y + 8, 10, Fade(highlight, 0.58f));
                ImageDrawLineEx(&image, (Vector2){(float)x, (float)(y + 13)}, (Vector2){(float)x, (float)(y + 26)}, 4, Fade((Color){96, 63, 33, 255}, 0.88f));
            }
        }
        break;
    case TILE_MOUNTAIN:
        for (int y = 24; y < height + 26; y += 58)
        {
            for (int x = 10; x < width + 40; x += 64)
            {
                Vector2 top = {(float)(x + 20), (float)(y - 22)};
                Vector2 left = {(float)x, (float)(y + 24)};
                Vector2 right = {(float)(x + 40), (float)(y + 24)};
                ImageDrawTriangle(&image, top, left, right, Fade(accent, 0.95f));
                ImageDrawTriangle(&image, top, (Vector2){(float)(x + 20), (float)(y + 24)}, right, Fade(lowlight, 0.95f));
                ImageDrawTriangle(&image, top, (Vector2){(float)(x + 9), (float)(y + 3)}, (Vector2){(float)(x + 26), (float)(y + 3)}, Fade(RAYWHITE, 0.85f));
            }
        }
        break;
    case TILE_DESERT:
        for (int y = 0; y < height; y += 38)
        {
            for (int x = -20; x < width + 20; x += 56)
            {
                const int offsetX = x + ((y / 38) % 2) * 12;
                ImageDrawCircle(&image, offsetX, y + 10, 10, Fade(accent, 0.42f));
                ImageDrawCircle(&image, offsetX + 12, y + 8, 8, Fade(accent, 0.36f));
                ImageDrawCircle(&image, offsetX + 23, y + 10, 7, Fade(accent, 0.32f));
                ImageDrawLineEx(&image, (Vector2){(float)x, (float)(y + 18)}, (Vector2){(float)(x + 36), (float)(y + 8)}, 3, Fade(highlight, 0.28f));
            }
        }
        break;
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

Texture2D BuildOceanTexture(int width, int height)
{
    Image image = GenImageColor(width, height, (Color){66, 152, 205, 255});

    for (int y = 0; y < height; y += 22)
    {
        Color band = (y / 22) % 2 == 0 ? Fade((Color){41, 121, 180, 255}, 0.24f) : Fade((Color){103, 185, 224, 255}, 0.18f);
        ImageDrawLineEx(&image, (Vector2){0.0f, (float)y}, (Vector2){(float)width, (float)(y + 12)}, 12, band);
    }

    for (int y = 20; y < height; y += 28)
    {
        for (int x = -40; x < width + 40; x += 58)
        {
            ImageDrawLineEx(&image, (Vector2){(float)x, (float)y}, (Vector2){(float)(x + 22), (float)(y - 6)}, 4, Fade((Color){221, 245, 255, 255}, 0.58f));
            ImageDrawLineEx(&image, (Vector2){(float)(x + 18), (float)(y + 6)}, (Vector2){(float)(x + 42), (float)(y + 1)}, 3, Fade((Color){27, 102, 160, 255}, 0.42f));
            ImageDrawLineEx(&image, (Vector2){(float)(x + 32), (float)(y + 11)}, (Vector2){(float)(x + 54), (float)(y + 6)}, 3, Fade((Color){233, 250, 255, 255}, 0.34f));
        }
    }

    for (int y = 14; y < height; y += 52)
    {
        for (int x = 8 + ((y / 52) % 2) * 24; x < width; x += 96)
        {
            ImageDrawCircle(&image, x, y, 22, Fade((Color){130, 205, 233, 255}, 0.12f));
            ImageDrawCircle(&image, x + 12, y + 6, 12, Fade((Color){223, 247, 255, 255}, 0.16f));
        }
    }

    for (int y = 24; y < height; y += 62)
    {
        for (int x = -10 + ((y / 62) % 2) * 30; x < width + 20; x += 88)
        {
            ImageDrawLineEx(&image, (Vector2){(float)x, (float)y}, (Vector2){(float)(x + 12), (float)(y - 10)}, 2, Fade((Color){235, 249, 255, 255}, 0.42f));
            ImageDrawLineEx(&image, (Vector2){(float)(x + 12), (float)(y - 10)}, (Vector2){(float)(x + 24), (float)(y - 2)}, 2, Fade((Color){235, 249, 255, 255}, 0.42f));
            ImageDrawLineEx(&image, (Vector2){(float)(x + 34), (float)(y + 8)}, (Vector2){(float)(x + 48), (float)(y - 4)}, 2, Fade((Color){220, 243, 255, 255}, 0.30f));
            ImageDrawLineEx(&image, (Vector2){(float)(x + 48), (float)(y - 4)}, (Vector2){(float)(x + 62), (float)(y + 6)}, 2, Fade((Color){220, 243, 255, 255}, 0.30f));
        }
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

void DrawTexturedHex(Texture2D texture, Vector2 center, float radius, Color tint)
{
    Vector2 corners[HEX_CORNERS];
    BuildHexPoints(center, radius, corners);
    float minX = corners[0].x;
    float maxX = corners[0].x;
    float minY = corners[0].y;
    float maxY = corners[0].y;

    for (int i = 1; i < HEX_CORNERS; i++)
    {
        if (corners[i].x < minX)
            minX = corners[i].x;
        if (corners[i].x > maxX)
            maxX = corners[i].x;
        if (corners[i].y < minY)
            minY = corners[i].y;
        if (corners[i].y > maxY)
            maxY = corners[i].y;
    }

    const float width = maxX - minX;
    const float height = maxY - minY;

    rlSetTexture(texture.id);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);

    const float centerU = (center.x - minX) / width;
    const float centerV = (center.y - minY) / height;

    for (int i = 0; i < HEX_CORNERS; i++)
    {
        const Vector2 a = corners[i];
        const Vector2 b = corners[(i + 1) % HEX_CORNERS];
        const float au = (a.x - minX) / width;
        const float av = (a.y - minY) / height;
        const float bu = (b.x - minX) / width;
        const float bv = (b.y - minY) / height;

        rlTexCoord2f(centerU, centerV);
        rlVertex2f(center.x, center.y);
        rlTexCoord2f(au, av);
        rlVertex2f(a.x, a.y);
        rlTexCoord2f(bu, bv);
        rlVertex2f(b.x, b.y);
    }

    rlEnd();
    rlSetTexture(0);
}

void DrawTileBaseBackground(enum TileType type, Vector2 center, float radius)
{
    DrawPoly(center, HEX_CORNERS, radius, -30.0f, ColorBrightness(TileBaseColor(type), -0.1f));
}

void DrawTileBackdrop(enum TileType type, Vector2 center, float radius)
{
    Color shade = Fade(ColorBrightness(TileAccentColor(type), 0.15f), 0.24f);
    DrawCircleGradient((int)center.x, (int)center.y, radius, Fade(shade, 0.55f), BLANK);
}

void DrawTilePattern(enum TileType type, Vector2 center, float radius)
{
    Color shade = Fade(ColorBrightness(TileAccentColor(type), -0.18f), 0.22f);

    switch (type)
    {
    case TILE_FOREST:
        for (int i = -1; i <= 1; i++)
        {
            DrawTriangle(
                (Vector2){center.x + i * radius * 0.32f, center.y - radius * 0.24f},
                (Vector2){center.x + i * radius * 0.18f - radius * 0.12f, center.y + radius * 0.14f},
                (Vector2){center.x + i * radius * 0.18f + radius * 0.12f, center.y + radius * 0.14f},
                shade);
        }
        break;
    case TILE_MOUNTAIN:
        DrawTriangle(
            (Vector2){center.x - radius * 0.34f, center.y + radius * 0.18f},
            (Vector2){center.x - radius * 0.02f, center.y - radius * 0.32f},
            (Vector2){center.x + radius * 0.24f, center.y + radius * 0.18f},
            shade);
        DrawTriangle(
            (Vector2){center.x - radius * 0.04f, center.y + radius * 0.18f},
            (Vector2){center.x + radius * 0.26f, center.y - radius * 0.20f},
            (Vector2){center.x + radius * 0.42f, center.y + radius * 0.18f},
            Fade(shade, 0.85f));
        break;
    case TILE_DESERT:
        DrawEllipse((int)center.x, (int)(center.y + radius * 0.12f), radius * 0.42f, radius * 0.16f, shade);
        break;
    default:
        DrawRing(center, radius * 0.28f, radius * 0.46f, 0.0f, 360.0f, 48, Fade(shade, 0.25f));
        break;
    }
}

void DrawTileIllustration(enum TileType type, Vector2 center, float radius)
{
    switch (type)
    {
    case TILE_FARMLAND:
    {
        const Color furrowDark = Fade((Color){158, 121, 34, 255}, 0.88f);
        const Color furrowLight = Fade((Color){220, 184, 77, 255}, 0.92f);
        const Color stalk = (Color){133, 98, 24, 255};
        const Color grain = (Color){245, 214, 110, 255};
        const float bandStep = radius * 0.10f;
        const float bandThickness = radius * 0.145f;

        for (int band = -7; band <= 7; band++)
        {
            float y = center.y + band * bandStep;
            float yOffset = y - center.y;
            float halfSpan = HexHalfWidthAtYOffset(radius * 0.96f, yOffset) * 0.98f;
            DrawLineEx(
                (Vector2){center.x - halfSpan, y + radius * 0.12f},
                (Vector2){center.x + halfSpan, y - radius * 0.12f},
                bandThickness,
                (band % 2 == 0) ? furrowLight : furrowDark);
            DrawLineEx(
                (Vector2){center.x - halfSpan * 0.97f, y + radius * 0.05f},
                (Vector2){center.x + halfSpan * 0.97f, y - radius * 0.22f},
                2.0f,
                Fade((Color){255, 235, 153, 255}, 0.45f));
        }

        for (int row = 0; row < 8; row++)
        {
            float y = center.y - radius * 0.70f + row * radius * 0.20f;
            float yOffset = y - center.y;
            float rowSpan = HexHalfWidthAtYOffset(radius * 0.94f, yOffset) * 0.92f;
            int stalkCount = fabsf(yOffset) > radius * 0.48f ? 6 : (fabsf(yOffset) > radius * 0.30f ? 8 : 11);
            for (int i = 0; i < stalkCount; i++)
            {
                float t = stalkCount == 1 ? 0.5f : (float)i / (float)(stalkCount - 1);
                float x = center.x - rowSpan + t * rowSpan * 2.0f + ((row % 2 == 0) ? 0.0f : radius * 0.035f);
                DrawLineEx((Vector2){x, y + radius * 0.15f}, (Vector2){x + radius * 0.01f, y - radius * 0.14f}, 3.0f, stalk);
                DrawLineEx((Vector2){x + radius * 0.01f, y - radius * 0.07f}, (Vector2){x - radius * 0.09f, y - radius * 0.16f}, 3.0f, grain);
                DrawLineEx((Vector2){x + radius * 0.02f, y - radius * 0.04f}, (Vector2){x + radius * 0.09f, y - radius * 0.13f}, 3.0f, grain);
                DrawLineEx((Vector2){x + radius * 0.01f, y + radius * 0.02f}, (Vector2){x - radius * 0.08f, y - radius * 0.06f}, 2.0f, Fade(grain, 0.92f));
                DrawLineEx((Vector2){x + radius * 0.02f, y + radius * 0.05f}, (Vector2){x + radius * 0.08f, y - radius * 0.03f}, 2.0f, Fade(grain, 0.92f));
            }
        }
    }
    break;
    case TILE_SHEEPMEADOW:
    {
        for (int band = -4; band <= 4; band++)
        {
            float y = center.y + band * radius * 0.15f;
            float halfSpan = HexHalfWidthAtYOffset(radius * 0.95f, y - center.y) * 0.98f;
            Color grass = (band % 2 == 0) ? Fade((Color){116, 181, 83, 255}, 0.72f) : Fade((Color){92, 160, 67, 255}, 0.70f);
            DrawLineEx((Vector2){center.x - halfSpan, y + radius * 0.10f}, (Vector2){center.x + halfSpan, y - radius * 0.08f}, radius * 0.18f, grass);
        }
        for (int row = 0; row < 7; row++)
        {
            float y = center.y - radius * 0.58f + row * radius * 0.18f;
            float rowSpan = HexHalfWidthAtYOffset(radius * 0.93f, y - center.y) * 0.90f;
            int tuftCount = fabsf(y - center.y) > radius * 0.38f ? 7 : 10;
            for (int i = 0; i < tuftCount; i++)
            {
                float t = tuftCount == 1 ? 0.5f : (float)i / (float)(tuftCount - 1);
                float x = center.x - rowSpan + t * rowSpan * 2.0f;
                DrawLineEx((Vector2){x, y + radius * 0.08f}, (Vector2){x - radius * 0.03f, y - radius * 0.03f}, 2.0f, (Color){66, 124, 46, 255});
                DrawLineEx((Vector2){x, y + radius * 0.08f}, (Vector2){x + radius * 0.01f, y - radius * 0.05f}, 2.0f, (Color){74, 136, 51, 255});
                DrawLineEx((Vector2){x, y + radius * 0.08f}, (Vector2){x + radius * 0.04f, y - radius * 0.02f}, 2.0f, (Color){66, 124, 46, 255});
            }
        }
        Vector2 sheepA = {center.x - radius * 0.42f, center.y + radius * 0.18f};
        Vector2 sheepB = {center.x + radius * 0.40f, center.y - radius * 0.28f};
        Vector2 sheepC = {center.x + radius * 0.42f, center.y + radius * 0.30f};
        DrawCircleV((Vector2){sheepA.x - radius * 0.09f, sheepA.y}, radius * 0.10f, RAYWHITE);
        DrawCircleV((Vector2){sheepA.x + radius * 0.01f, sheepA.y - radius * 0.01f}, radius * 0.11f, RAYWHITE);
        DrawCircleV((Vector2){sheepA.x + radius * 0.12f, sheepA.y + radius * 0.01f}, radius * 0.09f, RAYWHITE);
        DrawCircleV((Vector2){sheepA.x + radius * 0.21f, sheepA.y - radius * 0.01f}, radius * 0.06f, (Color){71, 61, 54, 255});
        DrawCircleV((Vector2){sheepB.x - radius * 0.08f, sheepB.y}, radius * 0.10f, RAYWHITE);
        DrawCircleV((Vector2){sheepB.x + radius * 0.02f, sheepB.y - radius * 0.01f}, radius * 0.11f, RAYWHITE);
        DrawCircleV((Vector2){sheepB.x + radius * 0.13f, sheepB.y}, radius * 0.09f, RAYWHITE);
        DrawCircleV((Vector2){sheepB.x + radius * 0.22f, sheepB.y - radius * 0.01f}, radius * 0.06f, (Color){71, 61, 54, 255});
        DrawCircleV((Vector2){sheepC.x - radius * 0.08f, sheepC.y}, radius * 0.09f, RAYWHITE);
        DrawCircleV((Vector2){sheepC.x + radius * 0.01f, sheepC.y - radius * 0.01f}, radius * 0.10f, RAYWHITE);
        DrawCircleV((Vector2){sheepC.x + radius * 0.11f, sheepC.y}, radius * 0.08f, RAYWHITE);
        DrawCircleV((Vector2){sheepC.x + radius * 0.18f, sheepC.y - radius * 0.01f}, radius * 0.05f, (Color){71, 61, 54, 255});
    }
    break;
    case TILE_MINE:
    {
        const Color brickDark = (Color){126, 64, 40, 255};
        const Color brickMid = (Color){174, 92, 58, 255};
        const Color brickLight = (Color){208, 132, 92, 255};
        const Color shadow = Fade((Color){70, 36, 22, 255}, 0.30f);
        DrawEllipse((int)center.x, (int)(center.y + radius * 0.48f), radius * 0.74f, radius * 0.18f, shadow);

        Rectangle bricks[15] = {
            {center.x - radius * 0.82f, center.y + radius * 0.30f, radius * 0.42f, radius * 0.16f},
            {center.x - radius * 0.30f, center.y + radius * 0.36f, radius * 0.42f, radius * 0.16f},
            {center.x + radius * 0.28f, center.y + radius * 0.30f, radius * 0.42f, radius * 0.16f},
            {center.x - radius * 0.62f, center.y + radius * 0.14f, radius * 0.40f, radius * 0.15f},
            {center.x - radius * 0.06f, center.y + radius * 0.18f, radius * 0.40f, radius * 0.15f},
            {center.x - radius * 0.70f, center.y - radius * 0.02f, radius * 0.42f, radius * 0.16f},
            {center.x - radius * 0.18f, center.y + radius * 0.02f, radius * 0.42f, radius * 0.16f},
            {center.x + radius * 0.42f, center.y + radius * 0.02f, radius * 0.32f, radius * 0.15f},
            {center.x - radius * 0.46f, center.y - radius * 0.20f, radius * 0.40f, radius * 0.15f},
            {center.x + radius * 0.02f, center.y - radius * 0.24f, radius * 0.38f, radius * 0.15f},
            {center.x - radius * 0.44f, center.y - radius * 0.20f, radius * 0.42f, radius * 0.16f},
            {center.x + radius * 0.16f, center.y - radius * 0.16f, radius * 0.38f, radius * 0.15f},
            {center.x - radius * 0.28f, center.y - radius * 0.36f, radius * 0.34f, radius * 0.14f},
            {center.x + radius * 0.10f, center.y - radius * 0.40f, radius * 0.36f, radius * 0.14f},
            {center.x - radius * 0.10f, center.y - radius * 0.56f, radius * 0.36f, radius * 0.14f}};

        for (int i = 0; i < 15; i++)
        {
            Rectangle brick = bricks[i];
            Rectangle side = {brick.x + brick.width * 0.74f, brick.y + radius * 0.01f, brick.width * 0.16f, brick.height};
            Rectangle top = {brick.x + radius * 0.02f, brick.y - radius * 0.03f, brick.width * 0.82f, radius * 0.04f};
            DrawRectangleRounded((Rectangle){brick.x + radius * 0.02f, brick.y + radius * 0.03f, brick.width, brick.height}, 0.12f, 6, Fade(BLACK, 0.10f));
            DrawRectangleRounded(brick, 0.12f, 6, i % 2 == 0 ? brickMid : brickLight);
            DrawRectangleRounded(side, 0.10f, 6, brickDark);
            DrawRectangleRounded(top, 0.10f, 6, Fade(brickLight, 0.96f));
            DrawRectangleLinesEx(brick, 1.4f, Fade(brickDark, 0.82f));
        }
    }
    break;
    case TILE_FOREST:
    {
        Vector2 trees[10] = {
            {center.x - radius * 0.56f, center.y + radius * 0.22f}, {center.x + radius * 0.55f, center.y + radius * 0.22f}, {center.x - radius * 0.28f, center.y - radius * 0.18f}, {center.x + radius * 0.27f, center.y - radius * 0.22f}, {center.x - radius * 0.68f, center.y - radius * 0.06f}, {center.x + radius * 0.68f, center.y - radius * 0.04f}, {center.x - radius * 0.02f, center.y + radius * 0.38f}, {center.x + radius * 0.30f, center.y + radius * 0.42f}, {center.x - radius * 0.44f, center.y - radius * 0.46f}, {center.x + radius * 0.47f, center.y - radius * 0.48f}};
        for (int i = 0; i < 10; i++)
        {
            Vector2 p = trees[i];
            DrawLineEx((Vector2){p.x, p.y + radius * 0.18f}, (Vector2){p.x, p.y + radius * 0.02f}, 6.0f, (Color){95, 61, 34, 255});
            DrawCircleV((Vector2){p.x, p.y - radius * 0.08f}, radius * 0.15f, (Color){31, 92, 46, 255});
            DrawCircleV((Vector2){p.x - radius * 0.10f, p.y}, radius * 0.11f, (Color){45, 118, 61, 255});
            DrawCircleV((Vector2){p.x + radius * 0.10f, p.y + radius * 0.01f}, radius * 0.10f, (Color){60, 136, 71, 255});
        }
    }
    break;
    case TILE_MOUNTAIN:
    {
        const Color rockDark = (Color){76, 80, 88, 255};
        const Color rockMid = (Color){108, 112, 121, 255};
        const Color rockLight = (Color){150, 154, 163, 255};
        const Color ridge = (Color){186, 190, 198, 255};
        const Color snow = (Color){236, 238, 242, 255};
        const Color baseShadow = Fade((Color){58, 62, 70, 255}, 0.72f);
        DrawLineEx((Vector2){center.x - radius * 0.92f, center.y + radius * 0.48f}, (Vector2){center.x + radius * 0.94f, center.y + radius * 0.48f}, 8.0f, baseShadow);

        Vector2 lTop = {center.x - radius * 0.34f, center.y - radius * 0.18f};
        Vector2 lLeft = {center.x - radius * 0.96f, center.y + radius * 0.48f};
        Vector2 lRight = {center.x + radius * 0.02f, center.y + radius * 0.48f};
        DrawTriangle(lTop, lLeft, lRight, rockDark);
        DrawTriangle(lTop, (Vector2){center.x - radius * 0.56f, center.y + radius * 0.08f}, (Vector2){center.x - radius * 0.18f, center.y + radius * 0.48f}, (Color){92, 96, 104, 255});

        Vector2 mTop = {center.x + radius * 0.18f, center.y - radius * 0.56f};
        Vector2 mLeft = {center.x - radius * 0.26f, center.y + radius * 0.50f};
        Vector2 mRight = {center.x + radius * 0.98f, center.y + radius * 0.50f};
        DrawTriangle(mTop, mLeft, mRight, rockMid);
        DrawTriangle(mTop, mLeft, (Vector2){center.x + radius * 0.06f, center.y + radius * 0.50f}, (Color){100, 104, 113, 255});
        DrawTriangle(mTop, (Vector2){center.x + radius * 0.06f, center.y + radius * 0.50f}, (Vector2){center.x + radius * 0.44f, center.y + radius * 0.50f}, rockLight);
        DrawTriangle(mTop, (Vector2){center.x + radius * 0.34f, center.y + radius * 0.16f}, mRight, (Color){94, 98, 106, 255});

        Vector2 rTop = {center.x + radius * 0.52f, center.y - radius * 0.18f};
        Vector2 rLeft = {center.x + radius * 0.14f, center.y + radius * 0.50f};
        Vector2 rRight = {center.x + radius * 0.74f, center.y + radius * 0.50f};
        DrawTriangle(rTop, rLeft, rRight, rockLight);
        DrawTriangle(rTop, (Vector2){center.x + radius * 0.34f, center.y + radius * 0.12f}, (Vector2){center.x + radius * 0.48f, center.y + radius * 0.50f}, ridge);

        DrawTriangle(mTop, (Vector2){center.x + radius * 0.06f, center.y - radius * 0.20f}, (Vector2){center.x + radius * 0.28f, center.y - radius * 0.20f}, snow);
        DrawTriangle(rTop, (Vector2){center.x + radius * 0.43f, center.y + radius * 0.02f}, (Vector2){center.x + radius * 0.58f, center.y + radius * 0.02f}, Fade(snow, 0.90f));
        DrawLineEx(mTop, (Vector2){center.x + radius * 0.30f, center.y + radius * 0.18f}, 2.5f, Fade((Color){220, 224, 230, 255}, 0.42f));
        DrawLineEx((Vector2){center.x - radius * 0.10f, center.y + radius * 0.08f}, (Vector2){center.x - radius * 0.18f, center.y + radius * 0.22f}, 1.5f, Fade((Color){62, 66, 74, 255}, 0.42f));
        DrawLineEx((Vector2){center.x + radius * 0.24f, center.y + radius * 0.04f}, (Vector2){center.x + radius * 0.16f, center.y + radius * 0.24f}, 1.5f, Fade((Color){62, 66, 74, 255}, 0.38f));
    }
    break;
    case TILE_DESERT:
    {
        for (int band = -4; band <= 4; band++)
        {
            float y = center.y + band * radius * 0.16f;
            float halfSpan = HexHalfWidthAtYOffset(radius * 0.95f, y - center.y) * 0.96f;
            DrawLineEx((Vector2){center.x - halfSpan, y + radius * 0.07f}, (Vector2){center.x + halfSpan, y - radius * 0.05f}, radius * 0.13f, (band % 2 == 0) ? Fade((Color){214, 182, 119, 255}, 0.62f) : Fade((Color){195, 156, 94, 255}, 0.58f));
        }
        DrawLineEx((Vector2){center.x - radius * 0.72f, center.y + radius * 0.12f}, (Vector2){center.x + radius * 0.10f, center.y - radius * 0.12f}, 5.0f, (Color){190, 149, 84, 255});
        DrawLineEx((Vector2){center.x - radius * 0.08f, center.y + radius * 0.28f}, (Vector2){center.x + radius * 0.74f, center.y + radius * 0.04f}, 5.0f, (Color){190, 149, 84, 255});
    }
    break;
    }
}

static float HexHalfWidthAtYOffset(float radius, float yOffset)
{
    const float absY = fabsf(yOffset);
    const float flatTopHeight = radius * 0.5f;
    if (absY >= radius)
    {
        return 0.0f;
    }
    if (absY <= flatTopHeight)
    {
        return radius * 0.8660254f;
    }

    const float t = (absY - flatTopHeight) / flatTopHeight;
    return radius * 0.8660254f * (1.0f - t);
}

static Color TileBaseColor(enum TileType type)
{
    switch (type)
    {
    case TILE_FARMLAND:
        return (Color){207, 174, 76, 255};
    case TILE_SHEEPMEADOW:
        return (Color){121, 181, 86, 255};
    case TILE_MINE:
        return (Color){186, 112, 72, 255};
    case TILE_FOREST:
        return (Color){68, 128, 79, 255};
    case TILE_MOUNTAIN:
        return (Color){123, 121, 128, 255};
    case TILE_DESERT:
        return (Color){223, 198, 136, 255};
    default:
        return LIGHTGRAY;
    }
}

static Color TileAccentColor(enum TileType type)
{
    switch (type)
    {
    case TILE_FARMLAND:
        return (Color){184, 138, 29, 255};
    case TILE_SHEEPMEADOW:
        return (Color){84, 148, 58, 255};
    case TILE_MINE:
        return (Color){146, 73, 46, 255};
    case TILE_FOREST:
        return (Color){37, 94, 50, 255};
    case TILE_MOUNTAIN:
        return (Color){91, 88, 94, 255};
    case TILE_DESERT:
        return (Color){199, 161, 93, 255};
    default:
        return GRAY;
    }
}
