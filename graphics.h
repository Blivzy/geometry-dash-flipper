/* graphics.h - procedural world, player, particle, and HUD rendering API. */
#pragma once
#include "geodash.h"

/**
 * Draws the whole world layer (background, parallax, objects, player, particles).
 * @param canvas target canvas (must be inside a draw callback)
 * @param w world snapshot (read only)
 * @param vertical true to rotate the rendered world 90 degrees (portrait viewport)
 * @param light_theme true for dark-on-light, false for light-on-dark
 * Side effects: sets canvas color and clears the canvas.
 */
void gfx_draw_world(Canvas* canvas, const World* w, bool vertical, bool light_theme);

/**
 * Draws the HUD in screen space (never shaken).
 * @param canvas target canvas
 * @param w world snapshot
 * @param light_theme theme flag matching gfx_draw_world
 */
void gfx_draw_hud(Canvas* canvas, const World* w, bool light_theme);

/**
 * Draws a small skull glyph (insane-mode marker) in screen space.
 * @param canvas target canvas
 * @param x left edge
 * @param y top edge
 */
void gfx_draw_skull(Canvas* canvas, int32_t x, int32_t y);

/**
 * Draws the cube character in screen space (menus, overlays).
 * @param canvas target canvas
 * @param x left edge of the 6*scale cube
 * @param y top edge
 * @param scale integer pixel scale (1 or 2)
 * @param rotation degrees; ignored when `airborne` is false
 * @param airborne true draws the tumbling filled-square variant
 * @param fg body colour
 * @param bg face (eye/mouth) colour
 */
void gfx_draw_cube_icon(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    int32_t scale,
    float rotation,
    bool airborne,
    Color fg,
    Color bg);

/** Difficulty faces shown in level select. */
typedef enum {
    GFX_FACE_EASY,
    GFX_FACE_NORMAL,
    GFX_FACE_HARD,
    GFX_FACE_HARDER,
    GFX_FACE_INSANE,
    GFX_FACE_DEMON
} GfxFace;

/**
 * Draws a 7x7 difficulty face with the current canvas colour.
 * @param canvas target canvas
 * @param x left edge
 * @param y top edge of the face (the demon's horns extend 2 rows above it)
 * @param face which face to draw
 */
void gfx_draw_face(Canvas* canvas, int32_t x, int32_t y, GfxFace face);
