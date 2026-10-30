/*
 * geodash.c - app lifecycle, ViewPort, event loop, and state machine.
 * Threading: physics runs on the MAIN thread. The loop sleeps on the input queue until
 * the next tick deadline (no timer thread, so ticks can never pile up behind a stall and
 * fast-forward the game). The draw callback copies g_world under the mutex and draws from
 * the copy, so a slow frame never blocks physics and physics never blocks a frame.
 * SD writes and ViewPort orientation changes happen after the mutex is released.
 */
#include "geodash.h"
#include "audio.h"
#include "game_core.h"
#include "graphics.h"
#include "menu.h"
#include "storage.h"
#include "theme.h"
#include <string.h>

#define TAG "GeoDash"
#define QUEUE_DEPTH 16
#define QUEUE_WAIT_MS 100
#define MAX_CATCHUP_TICKS 2
#define RESYNC_AFTER_TICKS 4

SaveData g_save;
World g_world;
AppState g_app_state = STATE_MENU;

static const NotificationSequence sequence_triple_vibro = {
    &message_vibro_on, &message_delay_50, &message_vibro_off, &message_delay_50,
    &message_vibro_on, &message_delay_50, &message_vibro_off, &message_delay_50,
    &message_vibro_on, &message_delay_50, &message_vibro_off, NULL,
};

static World g_draw_world; /* GUI-thread snapshot of g_world; only the draw callback touches it */

uint32_t geodash_tick_ms(uint8_t speed) {
    switch(speed) {
    case 1:
        return TICK_MS_SLOW;
    case 2:
        return TICK_MS_FAST;
    default:
        return TICK_MS_NORMAL;
    }
}

static bool transition_allowed(AppState from, AppState to) {
    if(to == STATE_MENU) return true;
    switch(from) {
    case STATE_MENU:
        return to == STATE_LEVEL_SELECT || to == STATE_SETTINGS;
    case STATE_LEVEL_SELECT:
        return to == STATE_PLAYING;
    case STATE_PLAYING:
        return to == STATE_PAUSED || to == STATE_DEAD || to == STATE_COMPLETE;
    case STATE_PAUSED:
    case STATE_DEAD:
        return to == STATE_PLAYING;
    case STATE_COMPLETE:
        return to == STATE_LEVEL_SELECT || to == STATE_PLAYING;
    default:
        return false;
    }
}

void geodash_vibrate(GeoApp* app, VibroKind kind) {
    if(kind == VIBRO_SHORT) {
        notification_message(app->notifications, &sequence_single_vibro);
    } else if(kind == VIBRO_DOUBLE) {
        notification_message(app->notifications, &sequence_double_vibro);
    } else {
        notification_message(app->notifications, &sequence_triple_vibro);
    }
}

void geodash_toast(GeoApp* app, const char* text) {
    app->toast = text;
    app->toast_ticks = TOAST_TICKS;
}

void geodash_apply_orientation(GeoApp* app, bool vertical) {
    if(app->view_vertical == vertical) return;
    app->view_vertical = vertical;
    view_port_set_orientation(
        app->view_port, vertical ? ViewPortOrientationVertical : ViewPortOrientationHorizontal);
}

void geodash_timer_run(GeoApp* app, bool run) {
    if(run && !app->ticking) {
        app->next_tick = furi_get_tick() + furi_ms_to_ticks(geodash_tick_ms(g_save.speed));
        app->ticking = true;
    } else if(!run) {
        app->ticking = false;
    }
}

void geodash_set_state(GeoApp* app, AppState next) {
    if(!transition_allowed(g_app_state, next)) return;
    const AppState prev = g_app_state;
    menu_exit(app, prev);
    g_app_state = next;
    app->want_vertical = next == STATE_PLAYING && g_save.orientation_vertical;
    geodash_timer_run(app, next == STATE_PLAYING);
    if(next == STATE_PLAYING) {
        game_block_held_key(&g_world, app->jump_key_down);
        audio_start_music(app->audio, g_world.level_id);
    } else if(next == STATE_PAUSED) {
        audio_stop_music(app->audio);
    }
    menu_enter(app, next);
    view_port_update(app->view_port);
}

static bool snapshot_light(const World* w) {
    return theme_light(w->level_id, g_save.light_theme);
}

static void draw_callback(Canvas* canvas, void* context) {
    GeoApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    const AppState state = g_app_state;
    if(state == STATE_PLAYING) {
        /* ~4 KB memcpy under the lock, then the expensive drawing runs lock-free */
        memcpy(&g_draw_world, &g_world, sizeof(World));
        const bool vertical = app->view_vertical;
        const bool light = snapshot_light(&g_draw_world);
        furi_mutex_release(app->mutex);
        gfx_draw_world(canvas, &g_draw_world, vertical, light);
        gfx_draw_hud(canvas, &g_draw_world, light);
        return;
    }
    if(state == STATE_PAUSED || state == STATE_DEAD || state == STATE_COMPLETE) {
        memcpy(&g_draw_world, &g_world, sizeof(World));
        gfx_draw_world(canvas, &g_draw_world, false, snapshot_light(&g_draw_world));
        menu_draw(canvas, app);
    } else {
        menu_draw(canvas, app);
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    GeoApp* app = context;
    GeoEvent ev = {.input = *event};
    furi_message_queue_put(app->queue, &ev, 0);
}

static void handle_game_events(GeoApp* app, uint32_t ev) {
    if(ev & EV_JUMP) audio_play_sfx(app->audio, SFX_JUMP);
    if(ev & EV_COIN) audio_play_sfx(app->audio, SFX_COIN);
    if(ev & EV_CHECKPOINT) audio_play_sfx(app->audio, SFX_CHECKPOINT);
    if(ev & EV_DEATH) {
        audio_stop_music(app->audio);
        audio_play_sfx(app->audio, SFX_DEATH);
        geodash_vibrate(app, VIBRO_SHORT);
    }
    if(ev & EV_COMPLETE) {
        audio_stop_music(app->audio);
        audio_play_sfx(app->audio, SFX_COMPLETE);
        geodash_vibrate(app, VIBRO_DOUBLE);
        if(!g_world.practice && !g_world.insane) g_save.best_percent[g_world.level_id] = 100;
        g_world.percent = 100;
        app->save_pending = true;
        geodash_set_state(app, STATE_COMPLETE);
        return;
    }
    if(ev & EV_DEAD_READY) {
        if(!g_world.practice && !g_world.insane &&
           g_world.percent > g_save.best_percent[g_world.level_id]) {
            g_save.best_percent[g_world.level_id] = g_world.percent;
        }
        geodash_set_state(app, STATE_DEAD);
    }
}

static void handle_tick(GeoApp* app) {
    if(g_app_state != STATE_PLAYING) return;
    handle_game_events(app, game_tick(&g_world));
    if(g_app_state != STATE_PLAYING) return;
    /* the GUI thread coalesces updates, so asking every tick never queues extra frames */
    if(++app->render_divider >= app->render_every) {
        app->render_divider = 0;
        view_port_update(app->view_port);
    }
}

/* Toast and the boot splash are both wall-clock countdowns in ms: the physics
 * clock is stopped on menus, so it cannot drive either of them. */
static void update_toast(GeoApp* app) {
    const uint32_t now = furi_get_tick();
    const uint32_t elapsed = now - app->last_loop_tick;
    app->last_loop_tick = now;
    if(app->boot_ticks > 0) {
        if(elapsed >= app->boot_ticks) {
            app->boot_ticks = 0;
            geodash_set_state(app, STATE_MENU);
        } else {
            app->boot_ticks = (uint16_t)(app->boot_ticks - elapsed);
            view_port_update(app->view_port); /* keeps the loading dots animating */
        }
    }
    if(app->toast_ticks == 0) return;
    if(elapsed >= app->toast_ticks) {
        app->toast_ticks = 0;
        view_port_update(app->view_port);
    } else {
        app->toast_ticks = (uint16_t)(app->toast_ticks - elapsed);
    }
}

/* Runs every tick whose deadline has passed (bounded), then resyncs after a long stall. */
static void run_due_ticks(GeoApp* app) {
    const uint32_t period = furi_ms_to_ticks(geodash_tick_ms(g_save.speed));
    uint8_t steps = 0;
    while(app->ticking && (int32_t)(furi_get_tick() - app->next_tick) >= 0 && steps < MAX_CATCHUP_TICKS) {
        app->next_tick += period;
        handle_tick(app);
        steps++;
    }
    if(app->ticking && (int32_t)(furi_get_tick() - app->next_tick) > (int32_t)(period * RESYNC_AFTER_TICKS)) {
        app->next_tick = furi_get_tick() + period; /* drop the backlog instead of fast-forwarding */
    }
}

static void playing_input(GeoApp* app, const InputEvent* e) {
    if(e->key == InputKeyUp || e->key == InputKeyOk) {
        if(e->type == InputTypePress) game_set_key(&g_world, true);
        if(e->type == InputTypeRelease) game_set_key(&g_world, false);
    } else if(e->key == InputKeyBack) {
        if(e->type == InputTypeShort) geodash_set_state(app, STATE_PAUSED);
        if(e->type == InputTypeLong) geodash_set_state(app, STATE_MENU);
    } else if(e->key == InputKeyLeft && e->type == InputTypePress) {
        /* practice-only manual save: drops a checkpoint under the player right now,
         * no need to land exactly on a level checkpoint object */
        if(game_force_checkpoint(&g_world)) {
            audio_play_sfx(app->audio, SFX_CHECKPOINT);
            geodash_vibrate(app, VIBRO_SHORT);
        }
    }
}

static void handle_input(GeoApp* app, const InputEvent* e) {
    if(g_app_state == STATE_BOOT) {
        if(e->type == InputTypePress) {
            app->boot_ticks = 0;
            geodash_set_state(app, STATE_MENU);
        }
        view_port_update(app->view_port);
        return;
    }
    if(e->key == InputKeyUp || e->key == InputKeyOk) {
        if(e->type == InputTypePress) app->jump_key_down = true;
        if(e->type == InputTypeRelease) app->jump_key_down = false;
    }
    if(g_app_state == STATE_PLAYING) {
        playing_input(app, e);
    } else {
        menu_input(app, e);
    }
    view_port_update(app->view_port);
}

int32_t geodash_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "start");
    GeoApp* app = malloc(sizeof(GeoApp));
    memset(app, 0, sizeof(*app));
    memset(&g_world, 0, sizeof(g_world));
    g_world.rng = 0x1234ABCDu;
    g_app_state = STATE_BOOT;
    app->running = true;
    app->storage_ok = save_load(&g_save);
    app->audio = audio_alloc();
    audio_set_muted(app->audio, g_save.muted);
    audio_set_volume(app->audio, g_save.music_volume);
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(QUEUE_DEPTH, sizeof(GeoEvent));
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_port = view_port_alloc();
    view_port_set_orientation(app->view_port, ViewPortOrientationHorizontal);
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->boot_ticks = BOOT_SPLASH_MS;
    audio_play_sfx(app->audio, SFX_BOOT);
    view_port_update(app->view_port);
    app->last_loop_tick = furi_get_tick();
    GeoEvent ev;
    while(app->running) {
        uint32_t wait_ms = QUEUE_WAIT_MS;
        if(app->ticking) {
            const int32_t left = (int32_t)(app->next_tick - furi_get_tick());
            wait_ms = left > 0 ? (uint32_t)left : 0;
        }
        const bool got = furi_message_queue_get(app->queue, &ev, furi_ms_to_ticks(wait_ms)) == FuriStatusOk;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(got) handle_input(app, &ev.input);
        run_due_ticks(app);
        update_toast(app);
        /* used to skip every other frame at Fast speed to cut draw_callback load; the
         * window_reset fix removed the actual cost driving that, so every tick renders
         * now -- Fast speed is a real ~62 Hz tick with a 1:1 render, not a half-rate one. */
        app->render_every = 1;
        furi_mutex_release(app->mutex);
        /* everything below can block (SD card, GUI lock), so it never runs under the mutex */
        geodash_apply_orientation(app, app->want_vertical);
        if(app->save_pending) {
            app->save_pending = false;
            save_write(&g_save);
        }
    }

    geodash_timer_run(app, false);
    audio_free(app->audio);
    save_write(&g_save);
    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    FURI_LOG_I(TAG, "stop");
    return 0;
}
