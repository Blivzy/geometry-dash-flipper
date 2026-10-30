/* level_data.h - level object types and the const level table. */
#pragma once
#include <stdint.h>

#define LEVEL_COUNT 9

typedef enum {
    OBJ_BLOCK = 0,
    OBJ_SPIKE,
    OBJ_PIT,
    OBJ_ORB_YELLOW,
    OBJ_ORB_PINK,
    OBJ_PAD_YELLOW,
    OBJ_PAD_PINK,
    OBJ_PORTAL_CUBE,
    OBJ_PORTAL_SHIP,
    OBJ_PORTAL_BALL,
    OBJ_PORTAL_UFO,
    OBJ_COIN,
    OBJ_CHECKPOINT,
    OBJ_FINISH,
    OBJ_PORTAL_WAVE /* appended so existing object ids never change */
} ObjType;

typedef struct {
    uint8_t type;
    int16_t gx;
    int8_t gy;
    uint8_t w;
    uint8_t h;
} LevelObj;

typedef struct {
    const char* name;
    const char* difficulty;
    uint8_t stars;
    uint16_t length_cells;
    uint16_t object_count;
    uint8_t music_tier;
    const LevelObj* objects;
} LevelInfo;

/**
 * Returns the level descriptor for an id.
 * @param id level index 0..LEVEL_COUNT-1 (clamped)
 * @return pointer to const level info; never NULL
 */
const LevelInfo* level_get(uint8_t id);
