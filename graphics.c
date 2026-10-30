/*
 * graphics.c - all drawing for GeoDash, using only Canvas primitives.
 * Algorithm: every world primitive is given in world pixels, converted to a
 * camera-relative rectangle, then optionally transposed (x<->y) for the vertical
 * viewport and offset by the screen-shake vector. Round shapes (portals, coins,
 * the rotating cube, the death ring) are short polylines drawn with canvas lines.
 * Animation is driven by w->elapsed_ticks so frames are deterministic.
 */
#include "graphics.h"
#include "theme.h"
#include <math.h>
#include <stdio.h>

#define SKULL_W 5
#define SKULL_H 5
#define HUD_MARGIN 1
#define SHAKE_MUL_X 37
#define SHAKE_MUL_Y 53
#define SHAKE_SPAN 5
#define DASH_STEP 8
#define HUD_TEXT_LEN 16
#define BAR_X 36
#define BAR_W 64
#define STAR_COUNT 12
#define SQUARE_COUNT 6
#define ATTEMPT_WORLD_X 64
#define ATTEMPT_MAX_TICKS 400
#define DEATH_RING_SPEED 1.7f
#define DEATH_FLASH_TICKS 2
#define TWO_PI 6.2831853f

typedef struct {
    Canvas* canvas;
    bool vertical;
    float cam;
    int32_t ox, oy;
    Color fg;
    Color bg;
    uint32_t t;
    Theme theme;
} Ctx;

static void rel_rect(const Ctx* c, int32_t rx, int32_t ry, int32_t w, int32_t h, bool fill) {
    if(w <= 0 || h <= 0) return;
    if(rx + w < 0 || rx > WORLD_W) return;
    int32_t sx = rx, sy = ry, sw = w, sh = h;
    if(c->vertical) {
        sx = ry;
        sy = rx;
        sw = h;
        sh = w;
    }
    sx += c->ox;
    sy += c->oy;
    if(fill) {
        canvas_draw_box(c->canvas, sx, sy, (size_t)sw, (size_t)sh);
    } else {
        canvas_draw_frame(c->canvas, sx, sy, (size_t)sw, (size_t)sh);
    }
}

static void rel_dot(const Ctx* c, int32_t x, int32_t y) {
    rel_rect(c, x, y, 1, 1, true);
}

static void rel_line(const Ctx* c, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if(c->vertical) {
        canvas_draw_line(c->canvas, y0 + c->ox, x0 + c->oy, y1 + c->ox, x1 + c->oy);
    } else {
        canvas_draw_line(c->canvas, x0 + c->ox, y0 + c->oy, x1 + c->ox, y1 + c->oy);
    }
}

static int32_t rel_x(const Ctx* c, float wx) {
    return (int32_t)floorf(wx - c->cam);
}

static int32_t iround(float v) {
    return (int32_t)floorf(v + 0.5f);
}

static void world_rect(const Ctx* c, float wx, float wy, int32_t w, int32_t h, bool fill) {
    rel_rect(c, rel_x(c, wx), (int32_t)wy, w, h, fill);
}

static void use_fg(const Ctx* c) {
    canvas_set_color(c->canvas, c->fg);
}

static void use_bg(const Ctx* c) {
    canvas_set_color(c->canvas, c->bg);
}

/* Closed ellipse polyline in camera-relative pixels. */
static void rel_ellipse(const Ctx* c, float cx, float cy, float hx, float hy, uint8_t segs, float rot) {
    int32_t px = 0, py = 0;
    for(uint8_t i = 0; i <= segs; i++) {
        const float a = rot + TWO_PI * (float)i / (float)segs;
        const int32_t x = iround(cx + cosf(a) * hx);
        const int32_t y = iround(cy + sinf(a) * hy);
        if(i > 0) rel_line(c, px, py, x, y);
        px = x;
        py = y;
    }
}

/* Square of half-side `half` rotated by `deg` around (cx, cy). */
static void rel_square_rot(const Ctx* c, float cx, float cy, float half, float deg) {
    const float base = deg * 0.0174533f + 0.7853982f;
    const float r = half * 1.4142136f;
    int32_t px[4], py[4];
    for(uint8_t k = 0; k < 4; k++) {
        const float a = base + (float)k * 1.5707963f;
        px[k] = iround(cx + cosf(a) * r);
        py[k] = iround(cy + sinf(a) * r);
    }
    for(uint8_t k = 0; k < 4; k++) {
        rel_line(c, px[k], py[k], px[(k + 1) & 3], py[(k + 1) & 3]);
    }
}

/* Faint dashed square used for the background layer. */
static void rel_ghost_square(const Ctx* c, int32_t x, int32_t y, int32_t s) {
    rel_rect(c, x, y, 2, 1, true);
    rel_rect(c, x + s - 2, y, 2, 1, true);
    rel_rect(c, x, y + s - 1, 2, 1, true);
    rel_rect(c, x + s - 2, y + s - 1, 2, 1, true);
    rel_rect(c, x, y, 1, 2, true);
    rel_rect(c, x + s - 1, y, 1, 2, true);
    rel_rect(c, x, y + s - 2, 1, 2, true);
    rel_rect(c, x + s - 1, y + s - 2, 1, 2, true);
}

/* ---- background layers: all faint, none brighter than gameplay objects ---- */

static int32_t wrap(int32_t v, int32_t span) {
    return ((v % span) + span) % span;
}

static void bg_stars(const Ctx* c) {
    const int32_t off = (int32_t)(c->cam / PARALLAX_FAR_DIV);
    for(int32_t i = 0; i < STAR_COUNT; i++) {
        if(((int32_t)(c->t / 10) + i * 5) % 9 == 0) continue; /* twinkle */
        const int32_t base = (i * WORLD_W) / STAR_COUNT + ((i * 37) % 10);
        rel_dot(c, wrap(base - off, WORLD_W), 3 + ((i * 23) % 34));
    }
}

static void bg_squares(const Ctx* c) {
    const int32_t off = (int32_t)(c->cam * 0.5f), span = WORLD_W + 24;
    for(int32_t i = 0; i < SQUARE_COUNT; i++) {
        const int32_t base = (i * span) / SQUARE_COUNT + ((i * 29) % 11);
        rel_ghost_square(c, wrap(base - off, span) - 12, 6 + ((i * 19) % 30), 6 + ((i * 5) % 7));
    }
}

static void bg_grid(const Ctx* c) {
    const int32_t off = wrap((int32_t)(c->cam * 0.5f), 16);
    for(int32_t x = -off; x < WORLD_W; x += 16) {
        for(int32_t y = 6; y < 46; y += 13) {
            rel_dot(c, x, y);
            rel_dot(c, x + 1, y);
            rel_dot(c, x, y + 1);
        }
    }
}

static void bg_triangles(const Ctx* c) {
    const int32_t off = (int32_t)(c->cam * 0.45f), span = WORLD_W + 24;
    for(int32_t i = 0; i < 6; i++) {
        const int32_t x = wrap((i * span) / 6 + ((i * 17) % 9) - off, span) - 12;
        const int32_t y = 8 + ((i * 23) % 28), h = 5 + (i % 3) * 2;
        rel_line(c, x, y + h, x + h, y + h);
        rel_line(c, x, y + h, x + h / 2, y);
        rel_line(c, x + h, y + h, x + h / 2, y);
    }
}

static void bg_city(const Ctx* c) {
    const int32_t off = (int32_t)(c->cam * 0.4f), span = WORLD_W + 40;
    for(int32_t i = 0; i < 7; i++) {
        const int32_t w = 12 + (i * 7) % 9, h = 12 + (i * 11) % 18;
        const int32_t x = wrap((i * span) / 7 - off, span) - 20, top = (int32_t)FLOOR_Y - h;
        for(int32_t k = 0; k < w; k += 4) rel_rect(c, x + k, top, 2, 1, true); /* dashed roof */
        for(int32_t y = top; y < (int32_t)FLOOR_Y; y += 4) {
            rel_dot(c, x, y);
            rel_dot(c, x + w, y);
        }
        for(int32_t wy = top + 4; wy < (int32_t)FLOOR_Y - 6; wy += 6) { /* windows */
            for(int32_t wx = x + 3; wx < x + w - 2; wx += 5) rel_dot(c, wx, wy);
        }
    }
}

static void bg_stripes(const Ctx* c) {
    const int32_t off = (int32_t)(c->cam * 0.5f), span = WORLD_W + 40;
    for(int32_t i = 0; i < 5; i++) {
        const int32_t x = wrap((i * span) / 5 - off, span) - 20;
        for(int32_t k = 0; k < 8; k++) rel_dot(c, x + k * 3, 6 + k * 4);
    }
}

static void bg_circles(const Ctx* c) {
    const int32_t off = (int32_t)(c->cam * 0.5f), span = WORLD_W + 30;
    for(int32_t i = 0; i < 4; i++) {
        const int32_t x = wrap((i * span) / 4 + ((i * 13) % 11) - off, span) - 15;
        const int32_t y = 10 + ((i * 17) % 22), r = 4 + (i % 3) * 2;
        rel_dot(c, x - r, y);
        rel_dot(c, x + r, y);
        rel_dot(c, x, y - r);
        rel_dot(c, x, y + r);
        rel_dot(c, x - r * 7 / 10, y - r * 7 / 10);
        rel_dot(c, x + r * 7 / 10, y - r * 7 / 10);
        rel_dot(c, x - r * 7 / 10, y + r * 7 / 10);
        rel_dot(c, x + r * 7 / 10, y + r * 7 / 10);
    }
}

/* Three dithered bands above the floor fade the sky toward the horizon (the 1-bit "gradient"). */
static void bg_horizon(const Ctx* c) {
    const int32_t fy = (int32_t)FLOOR_Y;
    const int32_t off = (int32_t)c->cam;
    for(int32_t x = wrap(-off, 2); x < WORLD_W; x += 2) rel_dot(c, x, fy - 2);
    for(int32_t x = wrap(-off, 4); x < WORLD_W; x += 4) rel_dot(c, x + 1, fy - 5);
    for(int32_t x = wrap(-off, 8); x < WORLD_W; x += 8) rel_dot(c, x + 2, fy - 9);
}

static void draw_background(const Ctx* c) {
    bg_horizon(c);
    bg_stars(c);
    switch(c->theme.bg) {
    case BG_GRID:
        bg_grid(c);
        break;
    case BG_TRIANGLES:
        bg_triangles(c);
        break;
    case BG_CITY:
        bg_city(c);
        break;
    case BG_STRIPES:
        bg_stripes(c);
        break;
    case BG_CIRCLES:
        bg_circles(c);
        break;
    default:
        bg_squares(c);
        break;
    }
}

/* ---- ground textures, all scroll 1:1 with the camera ---- */

static void ground_texture(const Ctx* c, int32_t fy) {
    const int32_t cam = (int32_t)c->cam;
    switch(c->theme.ground) {
    case GROUND_BRICK: {
        rel_rect(c, 0, fy + 3, WORLD_W, 1, true);
        rel_rect(c, 0, fy + 6, WORLD_W, 1, true);
        const int32_t off = wrap(cam, 16);
        for(int32_t x = -off; x < WORLD_W; x += 16) {
            rel_rect(c, x + 4, fy + 1, 1, 2, true);
            rel_rect(c, x + 12, fy + 4, 1, 2, true);
            rel_rect(c, x + 4, fy + 7, 1, 1, true);
        }
        break;
    }
    case GROUND_ZIGZAG: {
        const int32_t off = wrap(cam, 8);
        for(int32_t x = -off - 8; x < WORLD_W; x += 8) {
            rel_line(c, x, fy + 6, x + 4, fy + 3);
            rel_line(c, x + 4, fy + 3, x + 8, fy + 6);
        }
        break;
    }
    case GROUND_DOTS: {
        const int32_t off = wrap(cam, 4);
        for(int32_t x = -off; x < WORLD_W; x += 4) {
            rel_dot(c, x, fy + 3);
            rel_dot(c, x + 2, fy + 6);
        }
        break;
    }
    default: {
        const int32_t off = wrap(cam, DASH_STEP);
        for(int32_t x = -off - DASH_STEP; x < WORLD_W; x += DASH_STEP) {
            rel_rect(c, x + 1, fy + 3, 4, 1, true);
            rel_rect(c, x + 5, fy + 6, 3, 1, true);
        }
        break;
    }
    }
}

static void draw_floor(const Ctx* c, const World* w) {
    const int32_t fy = (int32_t)FLOOR_Y;
    use_fg(c);
    rel_rect(c, 0, fy, WORLD_W, 1, true);
    ground_texture(c, fy);
    for(int32_t x = -(wrap((int32_t)c->cam, 2)); x < WORLD_W; x += 4) rel_dot(c, x, fy + 1);
    use_bg(c);
    for(uint16_t i = 0; i < w->active_count; i++) {
        const LevelObj* o = &w->active[i].obj;
        if(o->type != OBJ_PIT) continue;
        world_rect(c, (float)o->gx * CELL_PX, FLOOR_Y, o->w * CELL_PX, WORLD_H - fy, true);
    }
    use_fg(c);
    for(uint16_t i = 0; i < w->active_count; i++) {
        const LevelObj* o = &w->active[i].obj;
        if(o->type != OBJ_PIT) continue;
        const int32_t lx = rel_x(c, (float)o->gx * CELL_PX);
        const int32_t rx = rel_x(c, (float)(o->gx + o->w) * CELL_PX) - 1;
        rel_rect(c, lx, fy, 1, WORLD_H - fy, true);
        rel_rect(c, rx, fy, 1, WORLD_H - fy, true);
        for(int32_t x = lx + 2; x < rx - 1; x += 3) rel_dot(c, x, WORLD_H - 2);
    }
}

/* One 8x8 block tile. The tile is always an 8x8 solid-edged cell: hitboxes never depend on style. */
static void draw_block_tile(const Ctx* c, int32_t rx, int32_t ry) {
    use_fg(c);
    rel_rect(c, rx, ry, CELL_PX, CELL_PX, true);
    use_bg(c);
    switch(c->theme.block) {
    case BLOCK_CROSS:
        rel_rect(c, rx + 1, ry + 1, 6, 6, false);
        use_fg(c);
        rel_line(c, rx + 2, ry + 2, rx + 5, ry + 5);
        rel_line(c, rx + 5, ry + 2, rx + 2, ry + 5);
        break;
    case BLOCK_SOLID:
        rel_dot(c, rx + 1, ry + 1);
        rel_dot(c, rx + CELL_PX - 2, ry + 1);
        rel_dot(c, rx + 1, ry + CELL_PX - 2);
        rel_dot(c, rx + CELL_PX - 2, ry + CELL_PX - 2);
        rel_rect(c, rx + 3, ry + 3, 2, 2, true);
        break;
    case BLOCK_STUD:
        rel_rect(c, rx + 2, ry + 2, 4, 4, false);
        break;
    default:
        rel_rect(c, rx + 1, ry + 1, 6, 6, false);
        rel_rect(c, rx + 3, ry + 3, 2, 2, true);
        break;
    }
    use_fg(c);
}

static void draw_block(const Ctx* c, const LevelObj* o) {
    for(int32_t cy = 0; cy < o->h; cy++) {
        for(int32_t cx = 0; cx < o->w; cx++) {
            const int32_t rx = rel_x(c, (float)(o->gx + cx) * CELL_PX);
            if(rx + CELL_PX < 0 || rx > WORLD_W) continue;
            draw_block_tile(c, rx, (o->gy + cy) * CELL_PX);
        }
    }
    use_fg(c);
}

/* Hazards must be readable at speed: a background-coloured halo is cut around a solid triangle. */
static void draw_spike(const Ctx* c, const LevelObj* o) {
    const bool down = o->gy < 4; /* ceiling spikes point down */
    const int32_t top = o->gy * CELL_PX;
    use_bg(c);
    for(int32_t i = 0; i < o->w; i++) {
        const int32_t rx = rel_x(c, (float)(o->gx + i) * CELL_PX);
        if(rx + CELL_PX < 0 || rx > WORLD_W) continue;
        for(int32_t r = -1; r < CELL_PX; r++) {
            const int32_t y = down ? top + CELL_PX - 1 - r : top + r;
            const int32_t half = r < 0 ? 1 : r / 2 + 1;
            rel_line(c, rx + 3 - half, y, rx + 4 + half, y);
        }
    }
    use_fg(c);
    for(int32_t i = 0; i < o->w; i++) {
        const int32_t rx = rel_x(c, (float)(o->gx + i) * CELL_PX);
        if(rx + CELL_PX < 0 || rx > WORLD_W) continue;
        for(int32_t r = 0; r < CELL_PX; r++) {
            const int32_t y = down ? top + CELL_PX - 1 - r : top + r;
            rel_line(c, rx + 3 - r / 2, y, rx + 4 + r / 2, y);
        }
    }
}

static void draw_orb(const Ctx* c, const LevelObj* o) {
    const int32_t rx = rel_x(c, (float)o->gx * CELL_PX), ry = o->gy * CELL_PX;
    const bool pulse = ((c->t / 12) & 1u) != 0;
    use_fg(c);
    rel_line(c, rx + 2, ry + 1, rx + 5, ry + 1);
    rel_line(c, rx + 2, ry + 6, rx + 5, ry + 6);
    rel_line(c, rx + 1, ry + 2, rx + 1, ry + 5);
    rel_line(c, rx + 6, ry + 2, rx + 6, ry + 5);
    if(o->type == OBJ_ORB_YELLOW) {
        rel_rect(c, rx + 3, ry + 3, 2, 2, true);
    } else {
        rel_dot(c, rx + 3, ry + 3);
    }
    if(pulse) { /* outer halo sparkle */
        rel_dot(c, rx, ry + 3);
        rel_dot(c, rx + 7, ry + 4);
        rel_dot(c, rx + 3, ry);
        rel_dot(c, rx + 4, ry + 7);
    } else {
        rel_dot(c, rx, ry);
        rel_dot(c, rx + 7, ry);
        rel_dot(c, rx, ry + 7);
        rel_dot(c, rx + 7, ry + 7);
    }
}

static void draw_pad(const Ctx* c, const LevelObj* o) {
    const int32_t rx = rel_x(c, (float)o->gx * CELL_PX);
    const bool up = o->gy >= 4;
    const int32_t ry = up ? o->gy * CELL_PX + CELL_PX - 3 : o->gy * CELL_PX;
    const bool solid = o->type == OBJ_PAD_YELLOW;
    use_fg(c);
    for(int32_t r = 0; r < 3; r++) {
        const int32_t row = up ? ry + r : ry + 2 - r; /* narrow side faces away from the wall */
        const int32_t inset = up ? 2 - r : r;
        if(solid) {
            rel_rect(c, rx + inset, row, CELL_PX - 2 * inset, 1, true);
        } else {
            rel_dot(c, rx + inset, row);
            rel_dot(c, rx + CELL_PX - 1 - inset, row);
            if(r == 2 && up) rel_rect(c, rx, row, CELL_PX, 1, true);
            if(r == 0 && !up) rel_rect(c, rx, row, CELL_PX, 1, true);
        }
    }
    use_bg(c);
    if(solid) rel_rect(c, rx + 3, up ? ry + 1 : ry + 1, 2, 1, true);
    use_fg(c);
}

static void draw_player_glyph(const Ctx* c, PlayerMode mode, int32_t x, int32_t y) {
    switch(mode) {
    case MODE_SHIP:
        rel_rect(c, x, y + 1, 5, 3, true);
        rel_dot(c, x + 5, y + 2);
        break;
    case MODE_BALL:
        rel_rect(c, x + 1, y, 3, 5, true);
        rel_rect(c, x, y + 1, 5, 3, true);
        break;
    case MODE_UFO:
        rel_rect(c, x, y + 2, 6, 2, true);
        rel_rect(c, x + 2, y, 2, 2, true);
        break;
    case MODE_WAVE:
        rel_line(c, x, y + 4, x + 5, y);
        rel_line(c, x, y + 4, x + 3, y + 4);
        rel_line(c, x + 3, y + 4, x + 5, y);
        rel_dot(c, x + 3, y + 2);
        break;
    default:
        rel_rect(c, x, y, 5, 5, true);
        break;
    }
}

static void draw_portal(const Ctx* c, const LevelObj* o) {
    const int32_t rx = rel_x(c, (float)o->gx * CELL_PX), ry = o->gy * CELL_PX;
    const int32_t h = o->h * CELL_PX;
    const float cx = (float)rx + 3.5f;
    use_fg(c);
    /* tall elliptical ring, 12 segments per side */
    int32_t pl = 0, pr = 0, py = 0;
    for(int32_t i = 0; i <= 12; i++) {
        const float u = (float)i / 12.0f * 2.0f - 1.0f;
        const float k = sqrtf(1.0f - u * u);
        const int32_t y = ry + iround((float)(h - 1) * (u + 1.0f) / 2.0f);
        const int32_t xl = iround(cx - 3.4f * k);
        const int32_t xr = iround(cx + 3.4f * k);
        if(i > 0) {
            rel_line(c, pl, py, xl, y);
            rel_line(c, pr, py, xr, y);
        }
        pl = xl;
        pr = xr;
        py = y;
    }
    /* marching shimmer through the ring */
    const int32_t phase = (int32_t)((c->t / 2) % 8);
    for(int32_t y = ry + 3 + phase; y < ry + h - 3; y += 8) rel_dot(c, rx + 3, y);
    draw_player_glyph(
        c, o->type == OBJ_PORTAL_WAVE ? MODE_WAVE : (PlayerMode)(o->type - OBJ_PORTAL_CUBE), rx + 1,
        ry + h / 2 - 3);
}

static void draw_coin(const Ctx* c, const LevelObj* o) {
    const int32_t rx = rel_x(c, (float)o->gx * CELL_PX), ry = o->gy * CELL_PX;
    static const float widths[4] = {3.0f, 2.0f, 0.8f, 2.0f};
    const float hx = widths[(c->t / 7 + (uint32_t)o->h * 2u) & 3u];
    use_fg(c);
    rel_ellipse(c, (float)rx + 3.5f, (float)ry + 3.5f, hx, 3.0f, 8, 0.0f);
    if(hx > 1.5f) rel_rect(c, rx + 3, ry + 2, 1, 3, true);
}

static void draw_checkpoint(const Ctx* c, const World* w, const LevelObj* o) {
    const int32_t rx = rel_x(c, (float)o->gx * CELL_PX), ry = o->gy * CELL_PX;
    const int32_t cy = ry + 8;
    const bool cleared = w->checkpoint.valid && (float)o->gx * CELL_PX <= w->checkpoint.x;
    use_fg(c);
    rel_line(c, rx + 3, cy - 4, rx + 6, cy);
    rel_line(c, rx + 6, cy, rx + 3, cy + 4);
    rel_line(c, rx + 3, cy + 4, rx, cy);
    rel_line(c, rx, cy, rx + 3, cy - 4);
    if(cleared) {
        /* banked checkpoint: diamond fills solid and gets a check tick above it */
        rel_rect(c, rx + 1, cy - 2, 4, 4, true);
        rel_line(c, rx - 1, cy - 7, rx + 1, cy - 5);
        rel_line(c, rx + 1, cy - 5, rx + 5, cy - 9);
    } else {
        rel_rect(c, rx + 2, cy - 1, 2, 2, true);
    }
    for(int32_t y = ry; y < ry + 16; y += 4) rel_dot(c, rx + 3, y);
}

static void draw_finish(const Ctx* c, const LevelObj* o) {
    const int32_t rx = rel_x(c, (float)o->gx * CELL_PX);
    use_fg(c);
    rel_rect(c, rx, 0, CELL_PX, WORLD_H, false);
    for(int32_t y = 0; y < WORLD_H; y += 2) {
        rel_rect(c, rx + 1 + (y / 2 % 2) * 2, y, 2, 2, true);
        rel_rect(c, rx + 4 - (y / 2 % 2) * 2, y, 2, 2, true);
    }
}

static void draw_cube(const Ctx* c, const Player* p, int32_t rx, int32_t ry) {
    if(p->on_ground) {
        /* solid cube with a face: two eyes and a mouth */
        rel_rect(c, rx, ry, 6, 6, true);
        use_bg(c);
        rel_rect(c, rx + 1, ry + 1, 1, 2, true);
        rel_rect(c, rx + 4, ry + 1, 1, 2, true);
        rel_rect(c, rx + 1, ry + 4, 4, 1, true);
        use_fg(c);
    } else {
        /* airborne: the cube visibly tumbles (filled with concentric squares) */
        const float cx = (float)rx + 3.0f, cy = (float)ry + 3.0f;
        for(float half = 3.0f; half >= 0.5f; half -= 0.75f) rel_square_rot(c, cx, cy, half, p->rotation);
        const float a = p->rotation * 0.0174533f;
        const float ca = cosf(a), sa = sinf(a);
        use_bg(c);
        const float ex[2] = {-1.5f, 1.5f};
        for(uint8_t i = 0; i < 2; i++) {
            const float ux = ex[i], uy = -1.0f;
            rel_dot(c, iround(cx + ux * ca - uy * sa - 0.5f), iround(cy + ux * sa + uy * ca - 0.5f));
        }
        use_fg(c);
    }
}

static void draw_ship(const Ctx* c, const World* w, int32_t rx, int32_t ry) {
    /* column spans {top, bottom} of the sprite: cockpit dome over a pointed hull */
    static const int8_t span[6][2] = {{3, 5}, {1, 5}, {0, 5}, {1, 4}, {3, 4}, {3, 3}};
    const Player* p = &w->player;
    float tilt = p->vy * 0.6f;
    if(tilt > 2.0f) tilt = 2.0f;
    if(tilt < -2.0f) tilt = -2.0f;
    for(int32_t col = 0; col < 6; col++) {
        const int32_t dy = iround(tilt * (float)(col - 1) / 4.0f);
        rel_line(c, rx + col, ry + span[col][0] + dy, rx + col, ry + span[col][1] + dy);
    }
    use_bg(c);
    rel_dot(c, rx + 2, ry + 2 + iround(tilt * 0.25f)); /* cockpit window */
    use_fg(c);
    if(w->jump_held && !w->jump_blocked) { /* thruster flame */
        const int32_t fl = (w->elapsed_ticks & 1u) ? 3 : 2;
        const int32_t fy = ry + 4 + iround(tilt * -0.5f);
        rel_line(c, rx - fl, fy, rx - 1, fy);
    }
}

static void draw_ball(const Ctx* c, const Player* p, int32_t rx, int32_t ry) {
    const float a = p->x * 0.45f * (p->gravity < 0.0f ? -1.0f : 1.0f);
    rel_rect(c, rx + 1, ry, 4, 6, true);
    rel_rect(c, rx, ry + 1, 6, 4, true);
    use_bg(c);
    const int32_t dx = iround(cosf(a) * 2.0f), dy = iround(sinf(a) * 2.0f);
    rel_line(c, rx + 3 - dx, ry + 3 - dy, rx + 2 + dx, ry + 2 + dy);
    use_fg(c);
}

static void draw_ufo(const Ctx* c, const World* w, int32_t rx, int32_t ry) {
    rel_rect(c, rx + 2, ry, 2, 1, true);
    rel_rect(c, rx + 1, ry + 1, 4, 2, true);
    use_bg(c);
    rel_rect(c, rx + 2, ry + 1, 2, 1, true);
    use_fg(c);
    rel_rect(c, rx, ry + 3, 6, 2, true);
    rel_rect(c, rx + 1, ry + 5, 4, 1, true);
    use_bg(c);
    rel_dot(c, rx + ((w->elapsed_ticks / 4) % 2 ? 1 : 4), ry + 3);
    use_fg(c);
}

static void draw_wave(const Ctx* c, const World* w, int32_t rx, int32_t ry) {
    /* Geometry Dash wave: a dart pointing up-right while held, down-right when released */
    const bool up = w->jump_held && !w->jump_blocked;
    const float ang = up ? -0.7853982f : 0.7853982f;
    const float ca = cosf(ang), sa = sinf(ang);
    const float cx = (float)rx + 3.0f, cy = (float)ry + 3.0f;
    static const float lx[4] = {4.4f, -3.0f, -1.0f, -3.0f};
    static const float ly[4] = {0.0f, -3.2f, 0.0f, 3.2f};
    int32_t px[4], py[4];
    for(uint8_t k = 0; k < 4; k++) {
        px[k] = iround(cx + lx[k] * ca - ly[k] * sa);
        py[k] = iround(cy + lx[k] * sa + ly[k] * ca);
    }
    for(float f = -3.2f; f <= 3.2f; f += 1.0f) { /* fill: fan of lines from the tip to the back edge */
        rel_line(c, px[0], py[0], iround(cx - 3.0f * ca - f * sa), iround(cy - 3.0f * sa + f * ca));
    }
    for(uint8_t k = 0; k < 4; k++) rel_line(c, px[k], py[k], px[(k + 1) & 3], py[(k + 1) & 3]);
    use_bg(c);
    rel_dot(c, iround(cx + 0.5f * ca), iround(cy + 0.5f * sa));
    use_fg(c);
}

static void draw_player(const Ctx* c, const World* w) {
    const Player* p = &w->player;
    const int32_t rx = rel_x(c, p->x), ry = (int32_t)p->y;
    use_fg(c);
    switch(p->mode) {
    case MODE_CUBE:
        draw_cube(c, p, rx, ry);
        break;
    case MODE_SHIP:
        draw_ship(c, w, rx, ry);
        break;
    case MODE_BALL:
        draw_ball(c, p, rx, ry);
        break;
    case MODE_UFO:
        draw_ufo(c, w, rx, ry);
        break;
    case MODE_WAVE:
        draw_wave(c, w, rx, ry);
        break;
    }
    use_fg(c);
}

static void draw_objects(const Ctx* c, const World* w) {
    for(uint16_t i = 0; i < w->active_count; i++) {
        const ActiveObj* a = &w->active[i];
        const LevelObj* o = &a->obj;
        switch(o->type) {
        case OBJ_BLOCK:
            draw_block(c, o);
            break;
        case OBJ_SPIKE:
            draw_spike(c, o);
            break;
        case OBJ_ORB_YELLOW:
        case OBJ_ORB_PINK:
            draw_orb(c, o);
            break;
        case OBJ_PAD_YELLOW:
        case OBJ_PAD_PINK:
            draw_pad(c, o);
            break;
        case OBJ_PORTAL_CUBE:
        case OBJ_PORTAL_SHIP:
        case OBJ_PORTAL_BALL:
        case OBJ_PORTAL_UFO:
        case OBJ_PORTAL_WAVE:
            draw_portal(c, o);
            break;
        case OBJ_COIN:
            if(!a->used) draw_coin(c, o);
            break;
        case OBJ_CHECKPOINT:
            if(w->practice) draw_checkpoint(c, w, o);
            break;
        case OBJ_FINISH:
            draw_finish(c, o);
            break;
        default:
            break;
        }
    }
}

static void draw_particles(const Ctx* c, const World* w) {
    use_fg(c);
    for(uint8_t i = 0; i < MAX_PARTICLES; i++) {
        const Particle* p = &w->particles[i];
        if(p->life == 0) continue;
        const int32_t x = rel_x(c, p->x), y = (int32_t)p->y;
        if(p->kind == 1 && p->life > PARTICLE_LIFE_SPARK / 2) {
            rel_rect(c, x, y, 2, 2, true);
        } else {
            rel_dot(c, x, y);
        }
    }
}

static void draw_attempt(const Ctx* c, const World* w) {
    if(c->vertical || w->attempt == 0 || w->elapsed_ticks > ATTEMPT_MAX_TICKS) return;
    char text[HUD_TEXT_LEN];
    const int32_t x = rel_x(c, (float)ATTEMPT_WORLD_X);
    if(x < -64 || x > WORLD_W) return;
    snprintf(text, sizeof(text), "Attempt %u", (unsigned)(w->attempt > 999 ? 999 : w->attempt));
    use_fg(c);
    canvas_set_font(c->canvas, FontPrimary);
    canvas_draw_str(c->canvas, x + c->ox, 22 + c->oy, text);
}

static void draw_death_fx(const Ctx* c, const World* w) {
    const Player* p = &w->player;
    if(p->alive || w->freeze_ticks == 0) return;
    const uint8_t age = (uint8_t)(FREEZE_TICKS - w->freeze_ticks);
    const float r = 2.0f + (float)age * DEATH_RING_SPEED;
    const int32_t cx = rel_x(c, p->x + 3.0f), cy = (int32_t)(p->y + 3.0f);
    use_fg(c);
    rel_ellipse(c, (float)cx, (float)cy, r, r, 12, 0.0f);
    if(age > 3) rel_ellipse(c, (float)cx, (float)cy, r * 0.55f, r * 0.55f, 8, 0.4f);
}

void gfx_draw_world(Canvas* canvas, const World* w, bool vertical, bool light_theme) {
    /* The Flipper's LCD has no backlight: it's reflective, so a mostly-black fill
     * reads as a dim, murky panel with the thin white ink nearly impossible to pick
     * out under normal light -- that was the "can barely see anything, only the
     * death flash looks bright" problem. Ink-on-paper is the only polarity that
     * actually reads on this screen, so it's fixed for every level now; the per-
     * level/user dark variant is retired (light_theme is kept as a parameter for
     * callers, just no longer flips the fill). */
    (void)light_theme;
    Ctx c = {
        .canvas = canvas,
        .vertical = vertical,
        .cam = w->camera_x,
        .ox = 0,
        .oy = 0,
        .fg = ColorBlack,
        .bg = ColorWhite,
        .t = w->elapsed_ticks,
        .theme = theme_for_level(w->level_id),
    };
    canvas_clear(canvas);
    if(w->shake_ticks > 0) {
        c.ox = ((int32_t)(w->shake_ticks * SHAKE_MUL_X) % SHAKE_SPAN) - SHAKE_AMPLITUDE;
        c.oy = ((int32_t)(w->shake_ticks * SHAKE_MUL_Y) % SHAKE_SPAN) - SHAKE_AMPLITUDE;
    }
    use_fg(&c);
    draw_background(&c);
    draw_floor(&c, w);
    draw_objects(&c, w);
    draw_attempt(&c, w);
    if(w->player.alive) draw_player(&c, w);
    draw_particles(&c, w);
    draw_death_fx(&c, w);
    if(!w->player.alive && w->freeze_ticks > FREEZE_TICKS - DEATH_FLASH_TICKS) {
        canvas_set_color(canvas, ColorXOR);
        canvas_draw_box(canvas, 0, 0, canvas_width(canvas), canvas_height(canvas));
        use_fg(&c);
    }
}

void gfx_draw_cube_icon(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    int32_t scale,
    float rotation,
    bool airborne,
    Color fg,
    Color bg) {
    const int32_t k = scale < 1 ? 1 : scale;
    canvas_set_color(canvas, fg);
    if(!airborne) {
        canvas_draw_box(canvas, x, y, (size_t)(6 * k), (size_t)(6 * k));
        canvas_set_color(canvas, bg);
        canvas_draw_box(canvas, x + k, y + k, (size_t)k, (size_t)(2 * k));
        canvas_draw_box(canvas, x + 4 * k, y + k, (size_t)k, (size_t)(2 * k));
        canvas_draw_box(canvas, x + k, y + 4 * k, (size_t)(4 * k), (size_t)k);
        canvas_set_color(canvas, fg);
        return;
    }
    Ctx c = {.canvas = canvas, .vertical = false, .cam = 0.0f, .ox = 0, .oy = 0, .fg = fg, .bg = bg, .t = 0};
    const float cx = (float)x + 3.0f * (float)k, cy = (float)y + 3.0f * (float)k;
    for(float half = 3.0f * (float)k; half >= 0.5f; half -= 0.5f) {
        rel_square_rot(&c, cx, cy, half, rotation);
    }
    const float a = rotation * 0.0174533f;
    const float ca = cosf(a), sa = sinf(a);
    canvas_set_color(canvas, bg);
    for(uint8_t i = 0; i < 2; i++) {
        const float ux = (i == 0 ? -1.5f : 1.5f) * (float)k, uy = -1.0f * (float)k;
        canvas_draw_box(
            canvas,
            iround(cx + ux * ca - uy * sa - 0.5f * (float)k),
            iround(cy + ux * sa + uy * ca - 0.5f * (float)k),
            (size_t)k,
            (size_t)k);
    }
    canvas_set_color(canvas, fg);
}

void gfx_draw_face(Canvas* canvas, int32_t x, int32_t y, GfxFace face) {
    /* 7x7 round face; `y` is the top of the face, horns/brows may use up to 2 rows above it */
    canvas_draw_line(canvas, x + 1, y, x + 5, y);
    canvas_draw_line(canvas, x + 1, y + 6, x + 5, y + 6);
    canvas_draw_line(canvas, x, y + 1, x, y + 5);
    canvas_draw_line(canvas, x + 6, y + 1, x + 6, y + 5);
    canvas_draw_dot(canvas, x + 2, y + 2);
    canvas_draw_dot(canvas, x + 4, y + 2);
    switch(face) {
    case GFX_FACE_EASY:
        canvas_draw_dot(canvas, x + 2, y + 4);
        canvas_draw_dot(canvas, x + 3, y + 5);
        canvas_draw_dot(canvas, x + 4, y + 4);
        break;
    case GFX_FACE_NORMAL:
        canvas_draw_line(canvas, x + 2, y + 4, x + 4, y + 4);
        break;
    case GFX_FACE_HARD:
        canvas_draw_dot(canvas, x + 2, y + 5);
        canvas_draw_dot(canvas, x + 3, y + 4);
        canvas_draw_dot(canvas, x + 4, y + 5);
        break;
    case GFX_FACE_HARDER:
        canvas_draw_line(canvas, x + 1, y + 1, x + 2, y + 2);
        canvas_draw_line(canvas, x + 5, y + 1, x + 4, y + 2);
        canvas_draw_line(canvas, x + 2, y + 5, x + 4, y + 5);
        break;
    case GFX_FACE_INSANE:
        canvas_draw_box(canvas, x + 1, y + 1, 5, 2);
        canvas_draw_line(canvas, x + 2, y + 4, x + 4, y + 4);
        canvas_draw_dot(canvas, x + 3, y + 5);
        break;
    default: /* demon: horns + fangs */
        canvas_draw_line(canvas, x + 1, y - 1, x, y - 2);
        canvas_draw_line(canvas, x + 5, y - 1, x + 6, y - 2);
        canvas_draw_line(canvas, x + 2, y + 4, x + 4, y + 4);
        canvas_draw_dot(canvas, x + 2, y + 5);
        canvas_draw_dot(canvas, x + 4, y + 5);
        break;
    }
}

void gfx_draw_skull(Canvas* canvas, int32_t x, int32_t y) {
    canvas_draw_box(canvas, x, y, SKULL_W, 1);
    canvas_draw_dot(canvas, x, y + 1);
    canvas_draw_dot(canvas, x + 2, y + 1);
    canvas_draw_dot(canvas, x + 4, y + 1);
    canvas_draw_box(canvas, x, y + 2, SKULL_W, 2);
    canvas_draw_box(canvas, x + 1, y + 4, SKULL_W - 2, 1);
}

void gfx_draw_hud(Canvas* canvas, const World* w, bool light_theme) {
    char text[HUD_TEXT_LEN];
    const int32_t width = canvas_width(canvas), height = canvas_height(canvas);
    static const char glyphs[] = {'C', 'S', 'B', 'U', 'W'};
    (void)light_theme; /* matches gfx_draw_world: always ink-on-paper, see note there */
    const Color ink = ColorBlack;
    const Color paper = ColorWhite;
    canvas_set_font(canvas, FontSecondary);
    const bool narrow = width < 100; /* vertical viewport is only 64 px wide */
    const int32_t bar_x = narrow ? HUD_MARGIN : BAR_X;
    const int32_t bar_w = narrow ? width - 2 * HUD_MARGIN : BAR_W;
    const int32_t text_y = narrow ? 5 : 1;
    /* backing plates keep the HUD readable on top of solid blocks */
    canvas_set_color(canvas, paper);
    canvas_draw_box(canvas, bar_x - 1, 0, (size_t)(bar_w + 2), 6);
    canvas_draw_box(canvas, 0, text_y - 1, 31, 9);
    canvas_draw_box(canvas, width - 22, text_y - 1, 22, 9);
    canvas_draw_box(canvas, HUD_MARGIN, height - 10, 8, 9);
    if(w->practice) canvas_draw_box(canvas, width / 2 - 21, height - 9, 42, 9);
    canvas_set_color(canvas, ink);
    /* Geometry Dash style progress bar: framed, filled from the left */
    canvas_draw_frame(canvas, bar_x, 0, (size_t)bar_w, 4);
    const int32_t fill = (bar_w - 2) * (int32_t)w->percent / 100;
    if(fill > 0) canvas_draw_box(canvas, bar_x + 1, 1, (size_t)fill, 2);
    if(w->best_before > 0 && w->best_before < 100) {
        canvas_draw_dot(canvas, bar_x + 1 + (bar_w - 2) * (int32_t)w->best_before / 100, 4);
    }
    snprintf(text, sizeof(text), "%u%%", (unsigned)w->percent);
    canvas_draw_str_aligned(canvas, width - HUD_MARGIN, text_y, AlignRight, AlignTop, text);
    snprintf(text, sizeof(text), "B:%u%%", (unsigned)w->best_before);
    canvas_draw_str_aligned(canvas, HUD_MARGIN, text_y, AlignLeft, AlignTop, text);
    text[0] = glyphs[w->player.mode];
    text[1] = '\0';
    canvas_draw_frame(canvas, HUD_MARGIN, height - 10, 8, 9);
    canvas_draw_str_aligned(canvas, HUD_MARGIN + 4, height - 5, AlignCenter, AlignCenter, text);
    if(w->practice) {
        canvas_draw_str_aligned(canvas, width / 2, height - 1, AlignCenter, AlignBottom, "PRACTICE");
        if(w->checkpoint.valid) { /* small check tick: a save is currently banked */
            const int32_t cx = width / 2 - 20, cby = height - 4;
            canvas_draw_line(canvas, cx, cby, cx + 1, cby + 2);
            canvas_draw_line(canvas, cx + 1, cby + 2, cx + 4, cby - 2);
        }
    }
    if(w->insane) {
        canvas_set_color(canvas, paper);
        canvas_draw_box(canvas, width - SKULL_W - HUD_MARGIN - 1, height - SKULL_H - HUD_MARGIN - 1, SKULL_W + 2, SKULL_H + 2);
        canvas_set_color(canvas, ink);
        gfx_draw_skull(canvas, width - SKULL_W - HUD_MARGIN, height - SKULL_H - HUD_MARGIN);
    }
}
