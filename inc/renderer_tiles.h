#ifndef RENDERER_TILES_H
#define RENDERER_TILES_H

#include "tile.h"
#include <raylib.h>

Texture2D BuildTerrainTexture(enum TileType type, int width, int height);
Texture2D BuildOceanTexture(int width, int height);
void DrawTexturedHex(Texture2D texture, Vector2 center, float radius, Color tint);
void DrawTileBaseBackground(enum TileType type, Vector2 center, float radius);
void DrawTileBackdrop(enum TileType type, Vector2 center, float radius);
void DrawTilePattern(enum TileType type, Vector2 center, float radius);
void DrawTileIllustration(enum TileType type, Vector2 center, float radius);

#endif
