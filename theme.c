/* theme.c - the per-level look table. Edit here to restyle a level. */
#include "theme.h"
#include "level_data.h"

static const Theme themes[LEVEL_COUNT] = {
    {BG_SQUARES, GROUND_DASH, BLOCK_FRAME, false},     /* 1 Stereo Madness */
    {BG_GRID, GROUND_DASH, BLOCK_FRAME, false},        /* 2 Back on Track */
    {BG_TRIANGLES, GROUND_ZIGZAG, BLOCK_CROSS, true},  /* 3 Polargeist */
    {BG_CITY, GROUND_BRICK, BLOCK_FRAME, false},       /* 4 Dry Out */
    {BG_STRIPES, GROUND_BRICK, BLOCK_STUD, false},     /* 5 Base After Base */
    {BG_CIRCLES, GROUND_DOTS, BLOCK_CROSS, true},      /* 6 Can't Let Go */
    {BG_GRID, GROUND_ZIGZAG, BLOCK_SOLID, false},      /* 7 Jumper */
    {BG_TRIANGLES, GROUND_DASH, BLOCK_STUD, true},     /* 8 Flipper */
    {BG_STRIPES, GROUND_ZIGZAG, BLOCK_SOLID, true},    /* 9 Wave Rush */
};

Theme theme_for_level(uint8_t level_id) {
    if(level_id >= LEVEL_COUNT) return themes[0];
    return themes[level_id];
}

bool theme_light(uint8_t level_id, bool user_light) {
    return user_light != theme_for_level(level_id).invert;
}
