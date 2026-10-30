/* geodash.h - shared constants, types, and the three allowed globals for GeoDash. */
#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdbool.h>
#include <stdint.h>
#include "level_data.h"

#define GEODASH_VERSION_STR "1.6"
#define SAVE_VERSION 2
#define SAVE_DIR_APPS "/ext/apps_data"
#define SAVE_DIR "/ext/apps_data/geodash"
#define SAVE_PATH "/ext/apps_data/geodash/save.dat"

/* Geometry (pixels; one grid cell = 8 px) */
#define CELL_PX 8
#define WORLD_W 128
#define WORLD_H 64
#define FLOOR_Y 56.0f
#define DEATH_Y 74.0f
#define PIT_DEATH_DEPTH 2.0f
#define PLAYER_SIZE 6.0f
#define PLAYER_INSET 1.0f
#define PLAYER_SCREEN_X 24.0f
#define WINDOW_AHEAD 192.0f
#define WINDOW_BEHIND 24.0f
#define MAX_ACTIVE_OBJECTS 256
#define MAX_PARTICLES 32
#define SPIKE_BOX_X 2.0f
#define SPIKE_BOX_W 4.0f
#define SPIKE_BOX_Y 3.0f
#define SPIKE_BOX_H 5.0f
#define SENSOR_PAD 2.0f
#define PAD_BOX_H 3.0f

/* Physics (px per 60 Hz tick) */
#define TICK_MS_FAST 16 /* original 1.0-1.3 pace (62.5 Hz) */
#define TICK_MS_NORMAL 22 /* default: ~73% of the old speed, ~8.5 cells/s */
#define TICK_MS_SLOW 28
#define SPEED_COUNT 3
#define SCROLL_SPEED 1.5f
#define INSANE_SPEED_MULT 1.5f
#define GRAVITY_CUBE 0.30f
#define GRAVITY_BALL 0.45f
#define GRAVITY_UFO 0.22f
#define GRAVITY_SHIP 0.25f
#define JUMP_VY 3.3f
#define UFO_JUMP_VY 2.6f
#define PAD_VY 4.2f
#define MAX_FALL_VY 4.5f
#define SHIP_THRUST 0.32f
#define SHIP_VY_MAX 4.0f
#define SHIP_DAMP 0.97f
#define UFO_COOLDOWN_TICKS 8
#define COYOTE_TICKS 5
#define JUMP_BUFFER_TICKS 5
#define INSANE_WINDOW_DIV 2
#define SPIN_DEG_PER_TICK 4.0f
#define FREEZE_TICKS 18
#define SHAKE_TICKS 10
#define SHAKE_AMPLITUDE 2
#define COMPLETE_DELAY_TICKS 20
#define ORIENTATION_COOLDOWN_MS 1000
#define TOAST_TICKS 1500 /* ms */
#define BOOT_SPLASH_MS 3200
#define COINS_PER_LEVEL 3

/* Particles */
#define DUST_COUNT 3
#define SPARK_COUNT 16
#define PARTICLE_LIFE_DUST 12
#define PARTICLE_LIFE_SPARK 24

/* Parallax */
#define PARALLAX_POINTS 12
#define PARALLAX_FAR_DIV 3.0f
#define PARALLAX_NEAR_MUL 0.6667f

/* Audio */
#define VOLUME_STEP 10
#define DEFAULT_VOLUME 60
#define INSANE_PITCH_MULT 1.1225f

typedef enum {
    STATE_BOOT,
    STATE_MENU,
    STATE_LEVEL_SELECT,
    STATE_SETTINGS,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_DEAD,
    STATE_COMPLETE
} AppState;

typedef enum { MODE_CUBE, MODE_SHIP, MODE_BALL, MODE_UFO, MODE_WAVE } PlayerMode;

typedef enum {
    SFX_JUMP,
    SFX_DEATH,
    SFX_COIN,
    SFX_CHECKPOINT,
    SFX_COMPLETE,
    SFX_MENU_MOVE,
    SFX_MENU_SELECT,
    SFX_INSANE,
    SFX_BOOT,
    SFX_COUNT
} SfxId;

typedef struct {
    uint8_t best_percent[LEVEL_COUNT];
    bool muted;
    bool orientation_vertical;
    uint8_t speed; /* 0 normal, 1 slow, 2 fast; reuses the old tilt byte so save v2 files stay valid */
    uint8_t music_volume;
    bool light_theme;
    uint8_t version;
} SaveData;

typedef struct {
    float x, y, vx, vy, gravity;
    bool on_ground;
    uint8_t coyote_frames;
    uint8_t jump_buffer;
    uint8_t ufo_cooldown;
    uint8_t trail_clock;
    PlayerMode mode;
    float rotation;
    bool alive;
} Player;

typedef struct {
    float x, y, vx, vy;
    uint8_t life;
    uint8_t kind;
} Particle;

typedef struct {
    LevelObj obj;
    bool used;
} ActiveObj;

typedef struct {
    float x, y, vy, gravity;
    PlayerMode mode;
    bool valid;
} Checkpoint;

typedef struct {
    Player player;
    float camera_x;
    const LevelInfo* level;
    uint8_t level_id;
    uint16_t next_index;
    uint16_t active_count;
    ActiveObj active[MAX_ACTIVE_OBJECTS];
    Particle particles[MAX_PARTICLES];
    Checkpoint checkpoint;
    bool practice;
    bool insane;
    bool jump_held;
    bool jump_blocked;
    uint8_t coins_mask;
    uint8_t shake_ticks;
    uint8_t freeze_ticks;
    uint8_t complete_ticks;
    bool finished;
    uint32_t elapsed_ticks;
    uint32_t rng;
    uint8_t percent;
    uint8_t best_before;
    uint16_t attempt;
} World;

extern SaveData g_save;
extern World g_world;
extern AppState g_app_state;

typedef struct Audio Audio;

typedef enum { VIBRO_SHORT, VIBRO_DOUBLE, VIBRO_TRIPLE } VibroKind;

typedef struct {
    InputEvent input;
} GeoEvent;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    NotificationApp* notifications;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    Audio* audio;
    bool running;
    bool storage_ok;
    bool show_about;
    bool left_held;
    bool view_vertical;
    bool ticking;
    bool save_pending;
    bool want_vertical;
    uint32_t next_tick;
    uint32_t last_loop_tick;
    bool jump_key_down;
    uint8_t render_divider;
    uint8_t render_every;
    uint8_t menu_cursor;
    uint8_t level_cursor;
    uint8_t settings_cursor;
    uint8_t pause_cursor;
    uint8_t end_cursor;
    bool practice_select;
    uint16_t toast_ticks;
    const char* toast;
    uint32_t last_orientation_tick;
    uint16_t anim;
    uint16_t boot_ticks; /* ms left on the startup splash; 0 = not showing it */
} GeoApp;

/**
 * Performs a validated state transition (runs exit then enter handlers).
 * @param app application context
 * @param next target state
 */
void geodash_set_state(GeoApp* app, AppState next);

/**
 * Fires a vibration pattern via the notification service.
 * @param app application context
 * @param kind pattern to play
 */
void geodash_vibrate(GeoApp* app, VibroKind kind);

/**
 * Shows a short toast message on the menu layer.
 * @param app application context
 * @param text static string; must outlive the toast
 */
void geodash_toast(GeoApp* app, const char* text);

/**
 * Applies the saved orientation to the ViewPort (vertical only while playing).
 * Must be called WITHOUT app->mutex held (the GUI thread may be inside the draw callback).
 * @param app application context
 * @param vertical true for portrait
 */
void geodash_apply_orientation(GeoApp* app, bool vertical);

/**
 * Starts or stops the fixed-step clock that paces game_tick on the main thread.
 * @param app application context
 * @param run true to run
 */
void geodash_timer_run(GeoApp* app, bool run);

/**
 * Milliseconds per physics tick for a speed setting (out-of-range -> normal).
 * @param speed SaveData.speed
 * @return tick period in ms
 */
uint32_t geodash_tick_ms(uint8_t speed);

/**
 * Application entry point (declared in application.fam).
 * @param p unused
 * @return 0 on clean exit
 */
int32_t geodash_app(void* p);
