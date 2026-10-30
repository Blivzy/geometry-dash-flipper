/*
 * storage.c - raw binary save file at /ext/apps_data/geodash/save.dat.
 * Algorithm: open the storage record per call, mkdir, read/write exactly
 * sizeof(SaveData) bytes, then clamp every field on load. Never trusts the file.
 */
#include "storage.h"
#include <string.h>

void save_set_defaults(SaveData* save) {
    memset(save, 0, sizeof(*save));
    save->music_volume = DEFAULT_VOLUME;
    save->version = SAVE_VERSION;
    save->speed = 2; /* Fast (~60fps tick+render) by default, was Normal (~45 Hz) */
}

/* Version 1 layout (8 levels) so existing progress survives the wave update. */
typedef struct {
    uint8_t best_percent[8];
    bool muted;
    bool orientation_vertical;
    bool tilt_control;
    uint8_t music_volume;
    bool light_theme;
    uint8_t version;
} SaveDataV1;

static void sanitize(SaveData* s) {
    for(uint8_t i = 0; i < LEVEL_COUNT; i++) {
        if(s->best_percent[i] > 100) s->best_percent[i] = 100;
    }
    s->muted = s->muted ? true : false;
    s->orientation_vertical = s->orientation_vertical ? true : false;
    if(s->speed >= SPEED_COUNT) s->speed = 0;
    s->light_theme = s->light_theme ? true : false;
    if(s->music_volume > 100) s->music_volume = 100;
}

bool save_load(SaveData* save) {
    save_set_defaults(save);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, SAVE_DIR_APPS);
    storage_common_mkdir(storage, SAVE_DIR);
    const bool dir_ok = storage_dir_exists(storage, SAVE_DIR);
    if(dir_ok) {
        File* file = storage_file_alloc(storage);
        if(storage_file_open(file, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
            uint8_t raw[sizeof(SaveData) > sizeof(SaveDataV1) ? sizeof(SaveData) : sizeof(SaveDataV1)];
            memset(raw, 0, sizeof(raw));
            const size_t got = storage_file_read(file, raw, sizeof(raw));
            if(got == sizeof(SaveData) && ((SaveData*)(void*)raw)->version == SAVE_VERSION) {
                SaveData tmp;
                memcpy(&tmp, raw, sizeof(tmp));
                sanitize(&tmp);
                *save = tmp;
            } else if(got == sizeof(SaveDataV1) && ((SaveDataV1*)(void*)raw)->version == 1) {
                SaveDataV1 old;
                memcpy(&old, raw, sizeof(old));
                SaveData tmp;
                save_set_defaults(&tmp);
                memcpy(tmp.best_percent, old.best_percent, sizeof(old.best_percent));
                tmp.muted = old.muted;
                tmp.orientation_vertical = old.orientation_vertical;
                tmp.music_volume = old.music_volume;
                tmp.light_theme = old.light_theme;
                sanitize(&tmp);
                *save = tmp;
            }
        }
        storage_file_close(file);
        storage_file_free(file);
    }
    furi_record_close(RECORD_STORAGE);
    return dir_ok;
}

bool save_write(const SaveData* save) {
    bool ok = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_dir_exists(storage, SAVE_DIR)) {
        File* file = storage_file_alloc(storage);
        if(storage_file_open(file, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            ok = storage_file_write(file, save, sizeof(*save)) == sizeof(*save);
        }
        storage_file_close(file);
        storage_file_free(file);
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}
