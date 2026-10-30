#pragma once
/**
 * theme.h - per-level visual identity (pure data, no drawing).
 * Every level picks one background layer, one ground texture and one block
 * look so levels are recognisable at a glance, like in Geometry Dash.
 * Themes are cosmetic only: they never influence physics or hitboxes.
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BG_SQUARES,   /* drifting dashed squares */
    BG_GRID,      /* dotted grid sliding by */
    BG_TRIANGLES, /* hollow triangles */
    BG_CITY,      /* dotted skyline */
    BG_STRIPES,   /* diagonal dashes */
    BG_CIRCLES,   /* ghost circles */
    BG_STYLE_COUNT
} BgStyle;

typedef enum {
    GROUND_DASH,   /* two rows of scrolling dashes */
    GROUND_BRICK,  /* brick courses */
    GROUND_ZIGZAG, /* zig-zag line */
    GROUND_DOTS,   /* dot lattice */
    GROUND_STYLE_COUNT
} GroundStyle;

typedef enum {
    BLOCK_FRAME, /* frame with a centre stud */
    BLOCK_CROSS, /* frame with an X */
    BLOCK_SOLID, /* solid with rivets */
    BLOCK_STUD,  /* ring around a stud */
    BLOCK_STYLE_COUNT
} BlockStyle;

typedef struct {
    BgStyle bg;
    GroundStyle ground;
    BlockStyle block;
    bool invert; /* flips the palette for this level: the only "colour" a 1-bit orange LCD has */
} Theme;

/**
 * Looks up the theme of a level.
 * @param level_id level index; out-of-range ids get the default theme
 * @return theme by value
 */
Theme theme_for_level(uint8_t level_id);

/**
 * Effective palette for a level: the user's Light/Dark setting XOR the level's own invert flag.
 * @param level_id level index
 * @param user_light true when Settings -> Theme is Light
 * @return true for dark-on-light rendering
 */
bool theme_light(uint8_t level_id, bool user_light);
