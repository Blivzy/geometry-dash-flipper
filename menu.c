/*
 * menu.c - main menu, level select, settings, about, pause/death/complete overlays.
 * Every state has enter/exit/draw/input handling here; PLAYING input and drawing
 * live in geodash.c/graphics.c. Cursor positions are kept in GeoApp.
 */
#include "menu.h"
#include "audio.h"
#include "game_core.h"
#include "graphics.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>

#define MAIN_ITEMS 4
#define PAUSE_ITEMS 3
#define END_ITEMS 2
#define SETTINGS_ITEMS 5
#define SETTINGS_ROW_STEP 10
#define VISIBLE_ROWS 4
#define ROW_H 11
#define TEXT_LEN 40

static const char* const main_labels[MAIN_ITEMS] = {"Play", "Settings", "About", "Exit"};
static const char* const pause_labels[PAUSE_ITEMS] = {"Resume", "Restart", "Quit"};

static uint8_t wrap_step(uint8_t cur, uint8_t count, bool down) {
    if(down) return (uint8_t)((cur + 1) % count);
    return cur == 0 ? (uint8_t)(count - 1) : (uint8_t)(cur - 1);
}

#define MENU_LIST_W 72
#define BAR_W_MENU 30

static void draw_arrow(Canvas* canvas, int32_t x, int32_t y) {
    canvas_draw_line(canvas, x, y, x, y + 4);
    canvas_draw_line(canvas, x + 1, y + 1, x + 1, y + 3);
    canvas_draw_dot(canvas, x + 2, y + 2);
}

static void draw_title_bar(Canvas* canvas, const char* title) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, WORLD_W, 12);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, WORLD_W / 2, 2, AlignCenter, AlignTop, title);
    canvas_set_color(canvas, ColorBlack);
}

/* One selectable row: inverted bar + arrow when selected. Leaves colour black. */
static void draw_row_bg(Canvas* canvas, int32_t y, int32_t w, bool selected) {
    if(selected) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, y, (size_t)w, ROW_H);
        canvas_set_color(canvas, ColorWhite);
        draw_arrow(canvas, 3, y + 3);
    } else {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_spike_icon(Canvas* canvas, int32_t x, int32_t base_y) {
    for(int32_t r = 0; r < 8; r++) {
        canvas_draw_line(canvas, x + 3 - r / 2, base_y - 7 + r, x + 4 + r / 2, base_y - 7 + r);
    }
}

/* Decorative scene: the cube hops one step every time the cursor moves. */
static void draw_menu_scene(Canvas* canvas, const GeoApp* app) {
    static const int32_t lift[4] = {0, 9, 15, 9};
    static const float rot[4] = {0.0f, 45.0f, 90.0f, 135.0f};
    const uint8_t phase = (uint8_t)(app->anim & 3u);
    const int32_t ground = 53;
    canvas_set_color(canvas, ColorBlack);
    for(int32_t y = 14; y < 56; y += 3) canvas_draw_dot(canvas, 74, y);
    canvas_draw_dot(canvas, 84, 20);
    canvas_draw_dot(canvas, 119, 24);
    canvas_draw_dot(canvas, 100, 17);
    canvas_draw_dot(canvas, 112, 33);
    canvas_draw_line(canvas, 76, ground, WORLD_W - 1, ground);
    for(int32_t x = 77; x < WORLD_W; x += 8) canvas_draw_line(canvas, x, ground + 3, x + 3, ground + 3);
    for(int32_t x = 81; x < WORLD_W; x += 8) canvas_draw_line(canvas, x, ground + 6, x + 2, ground + 6);
    draw_spike_icon(canvas, 105, ground);
    draw_spike_icon(canvas, 113, ground);
    gfx_draw_cube_icon(canvas, 86, ground - 12 - lift[phase], 2, rot[phase], phase != 0, ColorBlack, ColorWhite);
}

static void draw_main(Canvas* canvas, const GeoApp* app) {
    const bool toast = app->toast_ticks > 0 && app->toast != NULL;
    draw_title_bar(canvas, toast ? app->toast : "GEODASH");
    draw_menu_scene(canvas, app);
    canvas_set_font(canvas, FontPrimary);
    for(uint8_t i = 0; i < MAIN_ITEMS; i++) {
        const int32_t y = 15 + i * ROW_H;
        draw_row_bg(canvas, y, MENU_LIST_W, i == app->menu_cursor);
        canvas_draw_str(canvas, 10, y + 9, main_labels[i]);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_boot(Canvas* canvas, const GeoApp* app) {
    /* checkerboard grid panel, like GD's purple menu background -- just monochrome */
    canvas_set_color(canvas, ColorBlack);
    for(int32_t y = 14; y < WORLD_H; y += 10) canvas_draw_line(canvas, 0, y, WORLD_W - 1, y);
    for(int32_t x = 0; x < WORLD_W; x += 16) canvas_draw_line(canvas, x, 13, x, WORLD_H - 1);

    draw_title_bar(canvas, "GEODASH");
    canvas_set_color(canvas, ColorBlack);

    /* play-button glyph: diamond ring around a triangle, GD's icon shape, on a
     * paper-colored plate so the grid lines behind it don't cut through it */
    const int32_t cx = WORLD_W / 2, cy = 36;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, cx - 13, cy - 13, 27, 27);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_line(canvas, cx, cy - 11, cx + 11, cy);
    canvas_draw_line(canvas, cx + 11, cy, cx, cy + 11);
    canvas_draw_line(canvas, cx, cy + 11, cx - 11, cy);
    canvas_draw_line(canvas, cx - 11, cy, cx, cy - 11);
    canvas_draw_line(canvas, cx - 4, cy - 6, cx - 4, cy + 6);
    canvas_draw_line(canvas, cx - 4, cy - 6, cx + 6, cy);
    canvas_draw_line(canvas, cx - 4, cy + 6, cx + 6, cy);

    canvas_set_font(canvas, FontSecondary);
    char text[24];
    static const char* const dots[4] = {"", ".", "..", "..."};
    const uint16_t elapsed_ms = BOOT_SPLASH_MS - app->boot_ticks;
    snprintf(text, sizeof(text), "made by blivzy%s", dots[(elapsed_ms / 400) % 4]);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, WORLD_W / 2 - 38, WORLD_H - 9, 76, 9);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_str_aligned(canvas, WORLD_W / 2, WORLD_H - 2, AlignCenter, AlignBottom, text);
}

static void draw_about(Canvas* canvas) {
    draw_title_bar(canvas, "GEODASH v" GEODASH_VERSION_STR);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, 25, "Unofficial fan demake of");
    canvas_draw_str(canvas, 4, 35, "Geometry Dash. Not affiliated");
    canvas_draw_str(canvas, 4, 45, "with RobTop Games.");
    canvas_draw_str(canvas, 4, 60, "Back: return");
    gfx_draw_cube_icon(canvas, 112, 48, 1, 0.0f, false, ColorBlack, ColorWhite);
    draw_spike_icon(canvas, 104, 54);
}

static void draw_progress(Canvas* canvas, int32_t x, int32_t y, int32_t w, uint8_t pct) {
    canvas_draw_frame(canvas, x, y, (size_t)w, 5);
    const int32_t fill = (w - 2) * (int32_t)pct / 100;
    if(fill > 0) canvas_draw_box(canvas, x + 1, y + 1, (size_t)fill, 3);
}

static GfxFace face_for(const char* difficulty) {
    static const char* const names[] = {"Easy", "Normal", "Hard", "Harder", "Insane", "Demon"};
    for(uint8_t i = 0; i < 6; i++) {
        if(strcmp(difficulty, names[i]) == 0) return (GfxFace)i;
    }
    return GFX_FACE_NORMAL;
}

static void draw_level_select(Canvas* canvas, const GeoApp* app) {
    char text[TEXT_LEN];
    const LevelInfo* sel = level_get(app->level_cursor);
    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, ColorBlack);
    gfx_draw_face(canvas, 2, 2, face_for(sel->difficulty));
    snprintf(text, sizeof(text), "%s  %u*", sel->difficulty, (unsigned)sel->stars);
    canvas_draw_str(canvas, 12, 8, text);
    if(app->practice_select) {
        canvas_draw_box(canvas, 80, 0, 48, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 83, 8, "<PRACTICE>");
    } else {
        canvas_draw_str(canvas, 83, 8, "< NORMAL >");
    }
    canvas_set_color(canvas, ColorBlack);
    uint8_t first = 0;
    if(app->level_cursor >= VISIBLE_ROWS) first = (uint8_t)(app->level_cursor - VISIBLE_ROWS + 1);
    for(uint8_t row = 0; row < VISIBLE_ROWS; row++) {
        const uint8_t id = (uint8_t)(first + row);
        const LevelInfo* info = level_get(id);
        const int32_t y = 11 + row * ROW_H;
        const bool selected = id == app->level_cursor;
        draw_row_bg(canvas, y, WORLD_W, selected);
        snprintf(text, sizeof(text), "%u %s", (unsigned)(id + 1), info->name);
        canvas_draw_str(canvas, 9, y + 8, text);
        draw_progress(canvas, WORLD_W - BAR_W_MENU - 3, y + 3, BAR_W_MENU, g_save.best_percent[id]);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_settings(Canvas* canvas, const GeoApp* app) {
    char text[TEXT_LEN];
    draw_title_bar(canvas, "SETTINGS");
    canvas_set_font(canvas, FontSecondary);
    static const char* const names[SETTINGS_ITEMS] = {"Mute", "Orientation", "Theme", "Speed", "Volume"};
    for(uint8_t i = 0; i < SETTINGS_ITEMS; i++) {
        const int32_t y = 13 + i * SETTINGS_ROW_STEP;
        const bool selected = i == app->settings_cursor;
        draw_row_bg(canvas, y, WORLD_W, selected);
        canvas_draw_str(canvas, 9, y + 8, names[i]);
        switch(i) {
        case 0:
            snprintf(text, sizeof(text), "%s", g_save.muted ? "ON" : "off");
            break;
        case 1:
            snprintf(text, sizeof(text), "%s", g_save.orientation_vertical ? "Vertical" : "Horizontal");
            break;
        case 2:
            snprintf(text, sizeof(text), "%s", g_save.light_theme ? "Light" : "Dark");
            break;
        case 3:
            snprintf(
                text,
                sizeof(text),
                "%s",
                g_save.speed == 1 ? "Slow" : (g_save.speed == 2 ? "Fast (60fps)" : "Normal"));
            break;
        default:
            text[0] = '\0';
            break;
        }
        if(i < 4) {
            canvas_draw_str_aligned(canvas, WORLD_W - 3, y + 8, AlignRight, AlignBottom, text);
        } else {
            const int32_t segs = (int32_t)g_save.music_volume / VOLUME_STEP;
            for(int32_t k = 0; k < 10; k++) {
                if(k < segs) {
                    canvas_draw_box(canvas, 66 + k * 5, y + 3, 4, 5);
                } else {
                    canvas_draw_frame(canvas, 66 + k * 5, y + 3, 4, 5);
                }
            }
        }
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_overlay_frame(Canvas* canvas, const char* title) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 17, 7, 100, 56); /* drop shadow */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 14, 4, 100, 56);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 14, 4, 100, 56);
    canvas_draw_box(canvas, 14, 4, 100, 12);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, WORLD_W / 2, 6, AlignCenter, AlignTop, title);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_choice(Canvas* canvas, int32_t y, bool selected, const char* label) {
    if(selected) draw_arrow(canvas, 28, y - 6);
    canvas_draw_str(canvas, 36, y, label);
}

static void draw_pause(Canvas* canvas, const GeoApp* app) {
    draw_overlay_frame(canvas, "PAUSED");
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < PAUSE_ITEMS; i++) {
        draw_choice(canvas, 30 + i * 11, i == app->pause_cursor, pause_labels[i]);
    }
}

static void draw_dead(Canvas* canvas, const GeoApp* app) {
    char text[TEXT_LEN];
    snprintf(text, sizeof(text), "CRASHED #%u", (unsigned)(g_world.attempt > 999 ? 999 : g_world.attempt));
    draw_overlay_frame(canvas, text);
    canvas_set_font(canvas, FontSecondary);
    if(!g_world.practice && !g_world.insane && g_world.percent > g_world.best_before) {
        snprintf(text, sizeof(text), "%u%%  NEW BEST!", (unsigned)g_world.percent);
    } else {
        snprintf(text, sizeof(text), "%u%%   Best %u%%", (unsigned)g_world.percent, (unsigned)g_save.best_percent[g_world.level_id]);
    }
    canvas_draw_str_aligned(canvas, WORLD_W / 2, 19, AlignCenter, AlignTop, text);
    draw_progress(canvas, 24, 29, 80, g_world.percent);
    draw_choice(canvas, 45, app->end_cursor == 0, g_world.practice ? "Respawn" : "Retry");
    draw_choice(canvas, 55, app->end_cursor == 1, "Quit");
}

static void draw_coin_icon(Canvas* canvas, int32_t x, int32_t y, bool got) {
    canvas_draw_line(canvas, x + 2, y, x + 4, y);
    canvas_draw_line(canvas, x + 2, y + 6, x + 4, y + 6);
    canvas_draw_line(canvas, x, y + 2, x, y + 4);
    canvas_draw_line(canvas, x + 6, y + 2, x + 6, y + 4);
    canvas_draw_dot(canvas, x + 1, y + 1);
    canvas_draw_dot(canvas, x + 5, y + 1);
    canvas_draw_dot(canvas, x + 1, y + 5);
    canvas_draw_dot(canvas, x + 5, y + 5);
    if(got) canvas_draw_box(canvas, x + 2, y + 2, 3, 3);
}

static void draw_complete(Canvas* canvas, const GeoApp* app) {
    char text[TEXT_LEN];
    draw_overlay_frame(canvas, "LEVEL COMPLETE!");
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < COINS_PER_LEVEL; i++) {
        draw_coin_icon(canvas, 48 + i * 12, 19, ((g_world.coins_mask >> i) & 1u) != 0);
    }
    snprintf(
        text, sizeof(text), "Attempts %u  %lus", (unsigned)g_world.attempt,
        (unsigned long)(g_world.elapsed_ticks * geodash_tick_ms(g_save.speed) / 1000u));
    canvas_draw_str_aligned(canvas, WORLD_W / 2, 30, AlignCenter, AlignTop, text);
    draw_choice(canvas, 47, app->end_cursor == 0, "Continue");
    draw_choice(canvas, 56, app->end_cursor == 1, "Retry");
}

void menu_draw(Canvas* canvas, const GeoApp* app) {
    if(g_app_state == STATE_BOOT || g_app_state == STATE_MENU || g_app_state == STATE_LEVEL_SELECT ||
       g_app_state == STATE_SETTINGS) {
        canvas_clear(canvas);
    }
    canvas_set_color(canvas, ColorBlack);
    switch(g_app_state) {
    case STATE_BOOT:
        draw_boot(canvas, app);
        break;
    case STATE_MENU:
        if(app->show_about) {
            draw_about(canvas);
        } else {
            draw_main(canvas, app);
        }
        break;
    case STATE_LEVEL_SELECT:
        draw_level_select(canvas, app);
        break;
    case STATE_SETTINGS:
        draw_settings(canvas, app);
        break;
    case STATE_PAUSED:
        draw_pause(canvas, app);
        break;
    case STATE_DEAD:
        draw_dead(canvas, app);
        break;
    case STATE_COMPLETE:
        draw_complete(canvas, app);
        break;
    default:
        break;
    }
}

static void change_orientation(GeoApp* app) {
    const uint32_t now = furi_get_tick();
    if(now - app->last_orientation_tick < furi_ms_to_ticks(ORIENTATION_COOLDOWN_MS)) return;
    app->last_orientation_tick = now;
    g_save.orientation_vertical = !g_save.orientation_vertical;
}

static void settings_input(GeoApp* app, const InputEvent* e) {
    const bool step = e->type == InputTypeShort || e->type == InputTypeRepeat;
    if(!step) return;
    switch(e->key) {
    case InputKeyUp:
    case InputKeyDown:
        app->settings_cursor = wrap_step(app->settings_cursor, SETTINGS_ITEMS, e->key == InputKeyDown);
        audio_play_sfx(app->audio, SFX_MENU_MOVE);
        break;
    case InputKeyLeft:
    case InputKeyRight:
    case InputKeyOk:
        if(app->settings_cursor == 0) {
            g_save.muted = !g_save.muted;
            audio_set_muted(app->audio, g_save.muted);
        } else if(app->settings_cursor == 1) {
            change_orientation(app);
        } else if(app->settings_cursor == 2) {
            g_save.light_theme = !g_save.light_theme;
        } else if(app->settings_cursor == 3) {
            const int8_t dir = e->key == InputKeyLeft ? -1 : 1;
            g_save.speed = (uint8_t)((g_save.speed + SPEED_COUNT + dir) % SPEED_COUNT);
        } else if(e->key != InputKeyOk) {
            int16_t vol = (int16_t)g_save.music_volume + (e->key == InputKeyRight ? VOLUME_STEP : -VOLUME_STEP);
            if(vol < 0) vol = 0;
            if(vol > 100) vol = 100;
            g_save.music_volume = (uint8_t)vol;
            audio_set_volume(app->audio, g_save.music_volume);
        }
        audio_play_sfx(app->audio, SFX_MENU_SELECT);
        break;
    default:
        break;
    }
}

static void start_level(GeoApp* app) {
    g_world.attempt = 0;
    game_load_level(&g_world, app->level_cursor, app->practice_select);
    geodash_set_state(app, STATE_PLAYING);
}

static void main_input(GeoApp* app, const InputEvent* e) {
    if(e->key == InputKeyLeft) {
        if(e->type == InputTypePress) app->left_held = true;
        if(e->type == InputTypeRelease) app->left_held = false;
    }
    if(e->key == InputKeyOk && e->type == InputTypePress && app->left_held && !app->show_about) {
        g_world.insane = true;
        audio_set_pitch_shift(app->audio, true);
        audio_play_sfx(app->audio, SFX_INSANE);
        geodash_vibrate(app, VIBRO_TRIPLE);
        geodash_toast(app, "INSANE MODE");
        return;
    }
    if(e->type != InputTypeShort) return;
    if(app->show_about) {
        if(e->key == InputKeyBack || e->key == InputKeyOk) app->show_about = false;
        return;
    }
    switch(e->key) {
    case InputKeyUp:
    case InputKeyDown:
        app->menu_cursor = wrap_step(app->menu_cursor, MAIN_ITEMS, e->key == InputKeyDown);
        app->anim++;
        audio_play_sfx(app->audio, SFX_MENU_MOVE);
        break;
    case InputKeyBack:
        app->running = false;
        break;
    case InputKeyOk:
        audio_play_sfx(app->audio, SFX_MENU_SELECT);
        if(app->menu_cursor == 0) geodash_set_state(app, STATE_LEVEL_SELECT);
        else if(app->menu_cursor == 1) geodash_set_state(app, STATE_SETTINGS);
        else if(app->menu_cursor == 2) app->show_about = true;
        else app->running = false;
        break;
    default:
        break;
    }
}

static void level_select_input(GeoApp* app, const InputEvent* e) {
    if(e->type != InputTypeShort && e->type != InputTypeRepeat) return;
    switch(e->key) {
    case InputKeyUp:
    case InputKeyDown:
        app->level_cursor = wrap_step(app->level_cursor, LEVEL_COUNT, e->key == InputKeyDown);
        audio_play_sfx(app->audio, SFX_MENU_MOVE);
        break;
    case InputKeyRight:
    case InputKeyLeft:
        app->practice_select = !app->practice_select;
        audio_play_sfx(app->audio, SFX_MENU_MOVE);
        break;
    case InputKeyOk:
        audio_play_sfx(app->audio, SFX_MENU_SELECT);
        start_level(app);
        break;
    case InputKeyBack:
        geodash_set_state(app, STATE_MENU);
        break;
    default:
        break;
    }
}

static void pause_input(GeoApp* app, const InputEvent* e) {
    if(e->type != InputTypeShort) return;
    switch(e->key) {
    case InputKeyUp:
    case InputKeyDown:
        app->pause_cursor = wrap_step(app->pause_cursor, PAUSE_ITEMS, e->key == InputKeyDown);
        audio_play_sfx(app->audio, SFX_MENU_MOVE);
        break;
    case InputKeyBack:
        geodash_set_state(app, STATE_PLAYING);
        break;
    case InputKeyOk:
        audio_play_sfx(app->audio, SFX_MENU_SELECT);
        if(app->pause_cursor == 0) {
            geodash_set_state(app, STATE_PLAYING);
        } else if(app->pause_cursor == 1) {
            game_load_level(&g_world, g_world.level_id, g_world.practice);
            geodash_set_state(app, STATE_PLAYING);
        } else {
            geodash_set_state(app, STATE_MENU);
        }
        break;
    default:
        break;
    }
}

static void end_input(GeoApp* app, const InputEvent* e) {
    if(e->type != InputTypeShort) return;
    switch(e->key) {
    case InputKeyUp:
    case InputKeyDown:
        app->end_cursor = wrap_step(app->end_cursor, END_ITEMS, e->key == InputKeyDown);
        audio_play_sfx(app->audio, SFX_MENU_MOVE);
        break;
    case InputKeyBack:
        geodash_set_state(app, g_app_state == STATE_COMPLETE ? STATE_LEVEL_SELECT : STATE_MENU);
        break;
    case InputKeyOk:
        audio_play_sfx(app->audio, SFX_MENU_SELECT);
        if(g_app_state == STATE_COMPLETE) {
            if(app->end_cursor == 0) {
                geodash_set_state(app, STATE_LEVEL_SELECT);
            } else {
                game_load_level(&g_world, g_world.level_id, g_world.practice);
                geodash_set_state(app, STATE_PLAYING);
            }
        } else if(app->end_cursor == 0) {
            if(g_world.practice) {
                game_respawn(&g_world);
            } else {
                game_load_level(&g_world, g_world.level_id, false);
            }
            geodash_set_state(app, STATE_PLAYING);
        } else {
            geodash_set_state(app, STATE_MENU);
        }
        break;
    default:
        break;
    }
}

void menu_input(GeoApp* app, const InputEvent* e) {
    if(e->key == InputKeyBack && e->type == InputTypeLong && g_app_state != STATE_MENU) {
        geodash_set_state(app, STATE_MENU);
        return;
    }
    switch(g_app_state) {
    case STATE_MENU:
        main_input(app, e);
        break;
    case STATE_LEVEL_SELECT:
        level_select_input(app, e);
        break;
    case STATE_SETTINGS:
        if(e->key == InputKeyBack && e->type == InputTypeShort) {
            geodash_set_state(app, STATE_MENU);
        } else {
            settings_input(app, e);
        }
        break;
    case STATE_PAUSED:
        pause_input(app, e);
        break;
    case STATE_DEAD:
    case STATE_COMPLETE:
        end_input(app, e);
        break;
    default:
        break;
    }
}

void menu_enter(GeoApp* app, AppState state) {
    switch(state) {
    case STATE_MENU:
        app->show_about = false;
        app->left_held = false;
        audio_stop_music(app->audio);
        break;
    case STATE_LEVEL_SELECT:
        audio_stop_music(app->audio);
        break;
    case STATE_PAUSED:
        app->pause_cursor = 0;
        break;
    case STATE_DEAD:
    case STATE_COMPLETE:
        app->end_cursor = 0;
        break;
    default:
        break;
    }
}

void menu_exit(GeoApp* app, AppState state) {
    if(state == STATE_SETTINGS) {
        audio_set_muted(app->audio, g_save.muted);
        audio_set_volume(app->audio, g_save.music_volume);
        app->save_pending = true; /* written by the main loop, outside the draw mutex */
    }
}
