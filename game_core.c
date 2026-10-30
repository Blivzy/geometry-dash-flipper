/*
 * game_core.c - fixed-step physics, AABB collision, player modes, object window.
 * Algorithm: per tick -> mode-specific velocity update -> integrate Y -> resolve
 * vertical landings against blocks/floor/ceiling -> integrate X (constant scroll)
 * -> side collision + hazards + interactions. Objects stream through a sliding
 * window over the sorted level array so memory stays bounded.
 */
#include "game_core.h"
#include <math.h>
#include <string.h>

static float rng_unit(World* w) {
    w->rng ^= w->rng << 13;
    w->rng ^= w->rng >> 17;
    w->rng ^= w->rng << 5;
    return (float)(w->rng & 0xFFFFu) / 65535.0f;
}

static bool overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static float obj_left(const LevelObj* o) {
    return (float)o->gx * CELL_PX;
}

static float obj_right(const LevelObj* o) {
    return (float)(o->gx + o->w) * CELL_PX;
}

static float scroll_speed(const World* w) {
    return w->insane ? SCROLL_SPEED * INSANE_SPEED_MULT : SCROLL_SPEED;
}

static float gravity_magnitude(PlayerMode mode) {
    switch(mode) {
    case MODE_BALL:
        return GRAVITY_BALL;
    case MODE_UFO:
        return GRAVITY_UFO;
    case MODE_SHIP:
        return GRAVITY_SHIP;
    default:
        return GRAVITY_CUBE;
    }
}

static float gravity_sign(const Player* p) {
    return p->gravity < 0.0f ? -1.0f : 1.0f;
}

static void spawn_particle(World* w, float x, float y, float vx, float vy, uint8_t life, uint8_t kind) {
    uint8_t slot = 0;
    uint8_t lowest = 255;
    for(uint8_t i = 0; i < MAX_PARTICLES; i++) {
        if(w->particles[i].life < lowest) {
            lowest = w->particles[i].life;
            slot = i;
        }
    }
    Particle* p = &w->particles[slot];
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->life = life;
    p->kind = kind;
}

static void update_particles(World* w) {
    for(uint8_t i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &w->particles[i];
        if(p->life == 0) continue;
        p->x += p->vx;
        p->y += p->vy;
        if(p->kind == 1) p->vy += 0.15f;
        p->life--;
    }
}

static void window_fill(World* w) {
    const LevelObj* objs = w->level->objects;
    const uint16_t count = w->level->object_count;
    const float limit = w->camera_x + WORLD_W + WINDOW_AHEAD;
    while(w->next_index < count && obj_left(&objs[w->next_index]) < limit) {
        const LevelObj* o = &objs[w->next_index++];
        if(obj_right(o) < w->camera_x - WINDOW_BEHIND) continue;
        if(o->type == OBJ_COIN && (w->coins_mask & (1u << (o->h % COINS_PER_LEVEL)))) continue;
        if(w->active_count >= MAX_ACTIVE_OBJECTS) continue;
        w->active[w->active_count].obj = *o;
        w->active[w->active_count].used = false;
        w->active_count++;
    }
}

static void window_recycle(World* w) {
    uint16_t i = 0;
    while(i < w->active_count) {
        if(obj_right(&w->active[i].obj) < w->camera_x - WINDOW_BEHIND) {
            w->active[i] = w->active[w->active_count - 1];
            w->active_count--;
        } else {
            i++;
        }
    }
}

/* On a fresh level load this is a no-op scan (camera_x is 0, idx lands on 0).
 * On a checkpoint respawn deep into a level it skips straight to the first
 * object still in range instead of replaying thousands of already-passed
 * entries through window_fill's continue-and-discard path every single
 * respawn -- that replay was the main stutter practice mode produced on
 * every death past the first checkpoint. */
static void window_reset(World* w) {
    w->active_count = 0;
    const LevelObj* objs = w->level->objects;
    const uint16_t count = w->level->object_count;
    const float min_right = w->camera_x - WINDOW_BEHIND;
    uint16_t idx = 0;
    while(idx < count && obj_right(&objs[idx]) < min_right) idx++;
    w->next_index = idx;
    window_fill(w);
}

static void kill_player(World* w, uint32_t* ev) {
    if(!w->player.alive) return;
    w->player.alive = false;
    w->freeze_ticks = FREEZE_TICKS;
    w->shake_ticks = SHAKE_TICKS;
    for(uint8_t i = 0; i < SPARK_COUNT; i++) {
        float a = rng_unit(w) * 6.2831853f;
        float s = 0.6f + rng_unit(w) * 1.8f;
        spawn_particle(
            w, w->player.x + 3.0f, w->player.y + 3.0f, cosf(a) * s, sinf(a) * s,
            PARTICLE_LIFE_SPARK, 1);
    }
    *ev |= EV_DEATH;
}

static bool over_pit(const World* w, float center_x) {
    for(uint16_t i = 0; i < w->active_count; i++) {
        const LevelObj* o = &w->active[i].obj;
        if(o->type == OBJ_PIT && center_x >= obj_left(o) && center_x < obj_right(o)) return true;
    }
    return false;
}

static void do_jump(World* w, float vy_mag, uint32_t* ev) {
    Player* p = &w->player;
    p->vy = -gravity_sign(p) * vy_mag;
    p->on_ground = false;
    p->coyote_frames = 0;
    p->jump_buffer = 0;
    *ev |= EV_JUMP;
    for(uint8_t i = 0; i < DUST_COUNT; i++) {
        spawn_particle(
            w, p->x + 1.0f + (float)i, p->y + (gravity_sign(p) > 0 ? PLAYER_SIZE : 0.0f),
            -0.4f - 0.2f * (float)i, -0.1f * gravity_sign(p), PARTICLE_LIFE_DUST, 0);
    }
}

static void spawn_run_fx(World* w, bool landed) {
    Player* p = &w->player;
    const float sgn = gravity_sign(p);
    const float foot = p->y + (sgn > 0.0f ? PLAYER_SIZE : 0.0f);
    if(landed) {
        spawn_particle(w, p->x + 1.0f, foot, -0.5f, -0.15f * sgn, PARTICLE_LIFE_DUST, 0);
        spawn_particle(w, p->x + 4.0f, foot, -0.3f, -0.25f * sgn, PARTICLE_LIFE_DUST, 0);
        return;
    }
    if(++p->trail_clock < 3) return;
    p->trail_clock = 0;
    if(p->mode == MODE_SHIP) {
        spawn_particle(w, p->x - 1.0f, p->y + 4.0f, 0.0f, 0.0f, 9, 2);
    } else if(p->on_ground && p->mode != MODE_UFO && p->mode != MODE_WAVE) {
        spawn_particle(w, p->x, foot - sgn, -0.25f, -0.1f * sgn, 8, 0);
    }
}

static void update_velocity(World* w, uint32_t* ev) {
    Player* p = &w->player;
    const bool wants = w->jump_held && !w->jump_blocked;
    const uint8_t coyote_max = w->insane ? COYOTE_TICKS / INSANE_WINDOW_DIV : COYOTE_TICKS;
    p->gravity = gravity_sign(p) * gravity_magnitude(p->mode);
    if(p->ufo_cooldown > 0) p->ufo_cooldown--;
    switch(p->mode) {
    case MODE_CUBE:
        if(p->on_ground) p->coyote_frames = coyote_max;
        if((wants || p->jump_buffer > 0) && (p->on_ground || p->coyote_frames > 0)) {
            do_jump(w, JUMP_VY, ev);
        }
        p->vy += p->gravity;
        break;
    case MODE_SHIP:
        p->gravity = GRAVITY_SHIP;
        p->vy += wants ? -SHIP_THRUST : GRAVITY_SHIP;
        p->vy *= SHIP_DAMP;
        if(p->vy > SHIP_VY_MAX) p->vy = SHIP_VY_MAX;
        if(p->vy < -SHIP_VY_MAX) p->vy = -SHIP_VY_MAX;
        break;
    case MODE_BALL:
        if(p->on_ground) p->coyote_frames = coyote_max;
        if(p->jump_buffer > 0 && (p->on_ground || p->coyote_frames > 0)) {
            p->gravity = -p->gravity;
            p->on_ground = false;
            p->coyote_frames = 0;
            p->jump_buffer = 0;
            *ev |= EV_JUMP;
        }
        p->vy += p->gravity;
        break;
    case MODE_UFO:
        if(p->jump_buffer > 0 && p->ufo_cooldown == 0) {
            do_jump(w, UFO_JUMP_VY, ev);
            p->ufo_cooldown = UFO_COOLDOWN_TICKS;
        }
        p->vy += p->gravity;
        break;
    case MODE_WAVE:
        /* 45 degree dart: hold = climb, release = dive, same speed as the scroll */
        p->vy = wants ? -scroll_speed(w) : scroll_speed(w);
        break;
    }
    if(p->mode != MODE_SHIP && p->mode != MODE_WAVE) {
        if(p->vy > MAX_FALL_VY) p->vy = MAX_FALL_VY;
        if(p->vy < -MAX_FALL_VY) p->vy = -MAX_FALL_VY;
    }
}

static void land_on(Player* p, float surface_y, bool from_above) {
    p->y = from_above ? surface_y - PLAYER_SIZE : surface_y;
    p->vy = 0.0f;
    if((from_above && p->gravity >= 0.0f) || (!from_above && p->gravity < 0.0f)) {
        p->on_ground = true;
        p->rotation = roundf(p->rotation / 90.0f) * 90.0f;
    }
}

static void resolve_vertical(World* w, float prev_y) {
    Player* p = &w->player;
    const float prev_bottom = prev_y + PLAYER_SIZE;
    for(uint16_t i = 0; i < w->active_count; i++) {
        const LevelObj* o = &w->active[i].obj;
        if(o->type != OBJ_BLOCK) continue;
        const float bx = obj_left(o), by = (float)o->gy * CELL_PX;
        const float bw = (float)o->w * CELL_PX, bh = (float)o->h * CELL_PX;
        if(p->x + PLAYER_SIZE <= bx + 0.01f || p->x >= bx + bw - 0.01f) continue;
        if(prev_bottom <= by + 0.01f && p->y + PLAYER_SIZE > by) {
            land_on(p, by, true);
        } else if(prev_y >= by + bh - 0.01f && p->y < by + bh) {
            land_on(p, by + bh, false);
        }
    }
    if(p->y + PLAYER_SIZE >= FLOOR_Y && !over_pit(w, p->x + PLAYER_SIZE / 2.0f)) {
        land_on(p, FLOOR_Y, true);
    }
    if(p->y < 0.0f) land_on(p, 0.0f, false);
}

static bool box_hits_block(const World* w, float x, float y, float sz) {
    for(uint16_t i = 0; i < w->active_count; i++) {
        const LevelObj* o = &w->active[i].obj;
        if(o->type != OBJ_BLOCK) continue;
        if(overlap(
               x, y, sz, sz, obj_left(o), (float)o->gy * CELL_PX, (float)o->w * CELL_PX,
               (float)o->h * CELL_PX))
            return true;
    }
    return false;
}

static bool hits_spike(const World* w, float x, float y, float sz) {
    for(uint16_t i = 0; i < w->active_count; i++) {
        const LevelObj* o = &w->active[i].obj;
        if(o->type != OBJ_SPIKE) continue;
        const bool down = o->gy < 4;
        const float oy = (float)o->gy * CELL_PX + (down ? 0.0f : SPIKE_BOX_Y);
        for(uint8_t c = 0; c < o->w; c++) {
            const float ox = obj_left(o) + (float)c * CELL_PX + SPIKE_BOX_X;
            if(overlap(x, y, sz, sz, ox, oy, SPIKE_BOX_W, SPIKE_BOX_H)) return true;
        }
    }
    return false;
}

static void handle_interactions(World* w, uint32_t* ev) {
    Player* p = &w->player;
    const bool wants = w->jump_held && !w->jump_blocked;
    for(uint16_t i = 0; i < w->active_count; i++) {
        ActiveObj* a = &w->active[i];
        const LevelObj* o = &a->obj;
        const float ox = obj_left(o), oy = (float)o->gy * CELL_PX;
        switch(o->type) {
        case OBJ_ORB_YELLOW:
        case OBJ_ORB_PINK:
            if(!a->used && wants &&
               overlap(p->x, p->y, PLAYER_SIZE, PLAYER_SIZE, ox - SENSOR_PAD, oy - SENSOR_PAD,
                       CELL_PX + 2 * SENSOR_PAD, CELL_PX + 2 * SENSOR_PAD)) {
                a->used = true;
                if(o->type == OBJ_ORB_YELLOW) {
                    do_jump(w, JUMP_VY, ev);
                } else {
                    p->gravity = -p->gravity;
                    p->vy *= 0.5f;
                    p->on_ground = false;
                    *ev |= EV_JUMP;
                }
            }
            break;
        case OBJ_PAD_YELLOW:
        case OBJ_PAD_PINK: {
            const float py = o->gy < 4 ? oy : oy + CELL_PX - PAD_BOX_H;
            if(!a->used && overlap(p->x, p->y, PLAYER_SIZE, PLAYER_SIZE, ox, py, CELL_PX, PAD_BOX_H)) {
                a->used = true;
                if(o->type == OBJ_PAD_YELLOW) {
                    p->vy = -gravity_sign(p) * PAD_VY;
                } else {
                    p->gravity = -p->gravity;
                    p->vy = gravity_sign(p) * 0.5f;
                }
                p->on_ground = false;
                *ev |= EV_JUMP;
            }
            break;
        }
        case OBJ_PORTAL_CUBE:
        case OBJ_PORTAL_SHIP:
        case OBJ_PORTAL_BALL:
        case OBJ_PORTAL_UFO:
            if(!a->used && overlap(p->x, p->y, PLAYER_SIZE, PLAYER_SIZE, ox, oy, CELL_PX,
                                   (float)o->h * CELL_PX)) {
                a->used = true;
                p->mode = (PlayerMode)(o->type - OBJ_PORTAL_CUBE);
                p->gravity = gravity_magnitude(p->mode);
                p->on_ground = false;
            }
            break;
        case OBJ_COIN:
            if(!a->used && overlap(p->x, p->y, PLAYER_SIZE, PLAYER_SIZE, ox - SENSOR_PAD,
                                   oy - SENSOR_PAD, CELL_PX + 2 * SENSOR_PAD,
                                   CELL_PX + 2 * SENSOR_PAD)) {
                a->used = true;
                w->coins_mask |= (uint8_t)(1u << (o->h % COINS_PER_LEVEL));
                *ev |= EV_COIN;
            }
            break;
        case OBJ_CHECKPOINT:
            if(w->practice && !a->used &&
               overlap(p->x, p->y, PLAYER_SIZE, PLAYER_SIZE, ox, oy, CELL_PX, 2 * CELL_PX)) {
                a->used = true;
                w->checkpoint.x = p->x;
                w->checkpoint.y = p->y;
                w->checkpoint.vy = 0.0f;
                w->checkpoint.gravity = p->gravity;
                w->checkpoint.mode = p->mode;
                w->checkpoint.valid = true;
                *ev |= EV_CHECKPOINT;
            }
            break;
        case OBJ_PORTAL_WAVE:
            if(!a->used && overlap(p->x, p->y, PLAYER_SIZE, PLAYER_SIZE, ox, oy, CELL_PX,
                                   (float)o->h * CELL_PX)) {
                a->used = true;
                p->mode = MODE_WAVE;
                p->gravity = GRAVITY_CUBE;
                p->on_ground = false;
            }
            break;
        case OBJ_FINISH:
            if(overlap(p->x, p->y, PLAYER_SIZE, PLAYER_SIZE, ox, 0, CELL_PX, WORLD_H)) {
                w->finished = true;
                *ev |= EV_COMPLETE;
            }
            break;
        default:
            break;
        }
    }
}

static void reset_player(World* w, bool from_checkpoint) {
    Player* p = &w->player;
    memset(p, 0, sizeof(*p));
    p->alive = true;
    p->mode = MODE_CUBE;
    p->gravity = GRAVITY_CUBE;
    p->x = 8.0f;
    p->y = FLOOR_Y - PLAYER_SIZE;
    p->on_ground = true;
    if(from_checkpoint && w->checkpoint.valid) {
        p->x = w->checkpoint.x;
        p->y = w->checkpoint.y;
        p->vy = w->checkpoint.vy;
        p->gravity = w->checkpoint.gravity;
        p->mode = w->checkpoint.mode;
        p->on_ground = false;
    }
}

void game_load_level(World* w, uint8_t id, bool practice) {
    const bool insane = w->insane;
    const uint32_t rng = w->rng ? w->rng : 0x1234ABCDu;
    const uint16_t attempt = w->attempt;
    memset(w, 0, sizeof(*w));
    w->insane = insane;
    w->rng = rng;
    w->attempt = attempt;
    w->level_id = id < LEVEL_COUNT ? id : 0;
    w->level = level_get(w->level_id);
    w->practice = practice;
    w->best_before = g_save.best_percent[w->level_id];
    game_respawn(w);
}

void game_respawn(World* w) {
    reset_player(w, w->practice);
    if(w->attempt < 65535u) w->attempt++;
    w->camera_x = w->player.x - PLAYER_SCREEN_X;
    w->freeze_ticks = 0;
    w->shake_ticks = 0;
    w->finished = false;
    w->percent = game_percent(w);
    if(!w->practice) w->elapsed_ticks = 0;
    memset(w->particles, 0, sizeof(w->particles));
    window_reset(w);
}

bool game_force_checkpoint(World* w) {
    if(!w->practice || !w->player.alive) return false;
    const Player* p = &w->player;
    w->checkpoint.x = p->x;
    w->checkpoint.y = p->y;
    w->checkpoint.vy = 0.0f;
    w->checkpoint.gravity = p->gravity;
    w->checkpoint.mode = p->mode;
    w->checkpoint.valid = true;
    return true;
}

void game_set_key(World* w, bool down) {
    w->jump_held = down;
    if(!down) {
        w->jump_blocked = false;
    } else if(!w->jump_blocked) {
        w->player.jump_buffer = w->insane ? JUMP_BUFFER_TICKS / INSANE_WINDOW_DIV : JUMP_BUFFER_TICKS;
    }
}

void game_block_held_key(World* w, bool held) {
    w->jump_held = held;
    w->jump_blocked = held;
    w->player.jump_buffer = 0;
}

uint8_t game_percent(const World* w) {
    const float total = (float)w->level->length_cells * CELL_PX;
    float pct = total > 0.0f ? w->player.x * 100.0f / total : 0.0f;
    if(pct < 0.0f) pct = 0.0f;
    if(pct > 100.0f) pct = 100.0f;
    return (uint8_t)pct;
}

uint32_t game_tick(World* w) {
    uint32_t ev = 0;
    Player* p = &w->player;
    update_particles(w);
    if(!p->alive) {
        if(w->shake_ticks > 0) w->shake_ticks--;
        if(w->freeze_ticks > 0) {
            w->freeze_ticks--;
            if(w->freeze_ticks == 0) ev |= EV_DEAD_READY;
        }
        return ev;
    }
    if(w->shake_ticks > 0) w->shake_ticks--;
    w->elapsed_ticks++;
    const float prev_y = p->y;
    const bool was_grounded = p->on_ground;
    update_velocity(w, &ev);
    p->on_ground = false;
    p->y += p->vy;
    resolve_vertical(w, prev_y);
    spawn_run_fx(
        w, p->on_ground && !was_grounded && (p->mode == MODE_CUBE || p->mode == MODE_BALL));
    if(p->mode == MODE_WAVE) { /* dense zig-zag trail (cosmetic) */
        spawn_particle(w, p->x, p->y + 3.0f, 0.0f, 0.0f, 16, 2);
        spawn_particle(w, p->x - 0.75f, p->y + 3.0f - p->vy * 0.5f, 0.0f, 0.0f, 16, 2);
    }
    p->x += scroll_speed(w);
    w->camera_x = p->x - PLAYER_SCREEN_X;
    if(!p->on_ground && p->mode == MODE_CUBE) p->rotation += SPIN_DEG_PER_TICK * gravity_sign(p);
    if(p->coyote_frames > 0 && !p->on_ground) p->coyote_frames--;
    if(p->jump_buffer > 0) p->jump_buffer--;
    window_fill(w);
    window_recycle(w);
    const float bx = p->x + PLAYER_INSET, by = p->y + PLAYER_INSET;
    const float bs = PLAYER_SIZE - 2.0f * PLAYER_INSET;
    const bool in_pit = over_pit(w, p->x + PLAYER_SIZE / 2.0f) && p->y > FLOOR_Y - PIT_DEATH_DEPTH;
    if(box_hits_block(w, bx, by, bs) || hits_spike(w, bx, by, bs) || p->y > DEATH_Y || in_pit) {
        kill_player(w, &ev);
        return ev;
    }
    handle_interactions(w, &ev);
    w->percent = game_percent(w);
    return ev;
}
