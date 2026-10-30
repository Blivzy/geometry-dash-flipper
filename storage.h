/* storage.h - save/load of progress and settings to the SD card. */
#pragma once
#include "geodash.h"

/**
 * Fills a SaveData with defaults.
 * @param save output struct
 */
void save_set_defaults(SaveData* save);

/**
 * Loads and sanitizes the save file. Missing, short, or wrong-version files give defaults.
 * @param save output struct (always valid on return)
 * @return true if the SD card is usable (directory exists or was created)
 * Side effects: creates /ext/apps_data/geodash if absent.
 */
bool save_load(SaveData* save);

/**
 * Writes the save file.
 * @param save data to write
 * @return true on success; false if SD is absent or the write failed (caller ignores)
 */
bool save_write(const SaveData* save);
