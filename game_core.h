/* game_core.h - physics, collision, and player-mode API for GeoDash. */
#pragma once
#include "geodash.h"

#define EV_JUMP (1u << 0)
#define EV_DEATH (1u << 1)
#define EV_COIN (1u << 2)
#define EV_CHECKPOINT (1u << 3)
#define EV_COMPLETE (1u << 4)
#define EV_DEAD_READY (1u << 5)

/**
 * Loads a level into the world and resets all run state.
 * @param w world to reset
 * @param id level index (0..LEVEL_COUNT-1)
 * @param practice true for practice mode (checkpoints active)
 * Side effects: keeps g_world.insane; clears particles, checkpoint, coin mask.
 */
void game_load_level(World* w, uint8_t id, bool practice);

/**
 * Respawns the player at the last checkpoint (practice) or level start.
 * @param w world
 * Side effects: rebuilds the sliding object window; blocks a held jump key.
 */
void game_respawn(World* w);

/**
 * Force-saves a checkpoint at the player's current position, independent of
 * touching a level OBJ_CHECKPOINT. Practice-only; no-op outside practice or
 * while dead.
 * @param w world
 * @return true if a checkpoint was saved
 */
bool game_force_checkpoint(World* w);

/**
 * Reports the jump/thrust key state.
 * @param w world
 * @param down true on press, false on release
 * Side effects: arms the jump buffer on press; clears the phantom-jump block on release.
 */
void game_set_key(World* w, bool down);

/**
 * Marks the key as already held so it cannot trigger a jump until released.
 * @param w world
 * @param held true if the key is physically down right now
 */
void game_block_held_key(World* w, bool held);

/**
 * Advances the simulation one fixed 60 Hz step.
 * @param w world
 * @return bitmask of EV_* events produced during this tick
 */
uint32_t game_tick(World* w);

/**
 * Computes the progress percentage of the current run.
 * @param w world
 * @return 0..100
 */
uint8_t game_percent(const World* w);
