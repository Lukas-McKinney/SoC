#ifndef RENDERER_H
#define RENDERER_H

#include "map.h"
#include <raylib.h>

/* Loads textures, fonts, and render targets used by the renderer subsystem. */
void LoadRendererAssets(void);

/* Releases all renderer-owned textures, fonts, and render targets. */
void UnloadRendererAssets(void);

/* Processes board, build, trade, and modal input for the current frame. */
void HandleMapInput(struct Map *map);

/* Draws the board, pieces, overlays, and the game HUD for the current frame. */
void DrawMap(const struct Map *map);

/* Returns the UI font chosen by the renderer, or the default font if unavailable. */
Font RendererGetUiFont(void);

/* Produces a canonical world-space key for an edge so shared roads can be deduplicated. */
void RendererGetRoadEdgeKey(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by);

/* Produces a canonical world-space key for a corner so shared structures can be deduplicated. */
void RendererGetCornerKey(Vector2 center, float radius, int cornerIndex, int *x, int *y);

#endif
