/*
 * audio.c - non-blocking chiptune sequencer on its own FuriThread.
 * Algorithm: the thread polls every few ms. Music position is derived from the
 * wall clock (tick - start) modulo the loop length, so an SFX that borrows the
 * beeper automatically "skips" the notes it covered. Each music step plays a short
 * bass thump on the downbeat, then the melody note, then a small silent gap so
 * repeated notes are articulated instead of smeared into one buzz. The speaker is only
 * retuned on note edges (~25 calls/s), not every few ms. Mute suppresses speaker calls
 * but time keeps flowing.
 * IMPORTANT: the firmware only lets the thread that acquired the speaker call
 * furi_hal_speaker_start/stop/release (furi_check on ownership), so the audio
 * thread acquires, uses and releases the speaker itself.
 */
#include "audio.h"

#define AUDIO_STACK 3072
#define AUDIO_POLL_MS 3
#define SPEAKER_ACQUIRE_MS 100
#define TIER_COUNT 4
#define NO_TIER (-1)
#define MIN_HZ 100
#define MAX_HZ 8000
#define GAP_PERCENT 20 /* silent tail of every step */
#define BASS_HIT_MS 18 /* downbeat thump length */
#define BEATS_PER_BASS 4 /* one bass root per 4 steps */

typedef struct {
    uint16_t hz;
    uint16_t ms;
} Note;

typedef struct {
    const uint16_t* melody; /* one entry per step, 0 = rest */
    const uint16_t* bass;   /* one root per BEATS_PER_BASS steps, all >= MIN_HZ so they are audible */
    uint8_t steps;
    uint8_t step_ms;
} Track;

struct Audio {
    FuriThread* thread;
    volatile bool stop;
    volatile bool has_speaker;
    volatile bool muted;
    volatile bool shifted;
    volatile uint8_t volume;
    volatile int8_t tier;
    volatile uint32_t music_start;
    volatile int8_t sfx_pending;
};

/* Equal-tempered pitches in Hz. Anything below MIN_HZ is inaudible on the piezo. */
enum {
    N_B2 = 123, N_C3 = 131, N_D3 = 147, N_E3 = 165, N_F3 = 175, N_G3 = 196, N_A3 = 220,
    N_C5 = 523, N_D5 = 587, N_E5 = 659, N_F5 = 698, N_G5 = 784, N_A5 = 880, N_B5 = 988, N_C6 = 1047, N_D6 = 1175, N_E6 = 1319,
    N_A4 = 440, N_B4 = 494, N_Gs4 = 415, N_Gs5 = 831, N_Fs5 = 740, N_Ds5 = 622, N_Ds6 = 1245, N_REST = 0
};

/* Easy: C major, 8 chords (C G Am F C G F G), 140 ms steps */
static const uint16_t easy_melody[32] = {
    N_E5, N_G5, N_E5, N_C5, N_D5, N_G5, N_B5, N_G5, N_C5, N_E5, N_A5, N_E5, N_A5, N_C6, N_A5, N_F5,
    N_G5, N_E5, N_C5, N_E5, N_B5, N_G5, N_D5, N_G5, N_A5, N_F5, N_C5, N_F5, N_G5, N_REST,  N_B5, N_D6};
static const uint16_t easy_bass[8] = {N_C3, N_G3, N_A3, N_F3, N_C3, N_G3, N_F3, N_G3};

/* Medium: A minor, 8 chords (Am F C G Am F E E), 115 ms steps */
static const uint16_t med_melody[32] = {
    N_A4, N_C5, N_E5, N_A5, N_F5, N_E5, N_C5, N_A4, N_C5, N_E5, N_G5, N_E5, N_D5, N_G5, N_B5, N_D6,
    N_E5, N_A5, N_C6, N_A5, N_C6, N_A5, N_F5, N_A5, N_B4, N_E5, N_Gs4, N_B4, N_Gs5, N_E5, N_B4, N_E5};
static const uint16_t med_bass[8] = {N_A3, N_F3, N_C3, N_G3, N_A3, N_F3, N_E3, N_E3};

/* Hard: E minor, 12 chords (Em C G D Em C B B Em C D B), 90 ms steps */
static const uint16_t hard_melody[48] = {
    N_E5, N_REST,  N_G5, N_B5, N_C6, N_B5, N_G5, N_E5, N_D5, N_G5, N_B5, N_G5, N_Fs5, N_A5, N_D6, N_A5,
    N_B5, N_G5, N_E5, N_G5, N_E5, N_G5, N_C6, N_G5, N_Ds5, N_Fs5, N_B5, N_Fs5, N_B5, N_A5, N_Fs5, N_Ds5,
    N_E5, N_G5, N_B5, N_E6, N_C6, N_REST,  N_G5, N_E5, N_Fs5, N_A5, N_D6, N_A5, N_B5, N_Ds6, N_B5, N_Fs5};
static const uint16_t hard_bass[12] = {N_E3, N_C3, N_G3, N_D3, N_E3, N_C3, N_B2, N_B2, N_E3, N_C3, N_D3, N_B2};

/* Demon: D phrygian-dominant, 8 roots (Dm F Am G Dm B F G), 85 ms steps.
 * The 3 demon levels (Jumper/Flipper/Wave Rush) used to share hard_melody with
 * Base After Base and Can't Let Go; this gives the demon tier its own, more
 * dissonant identity (raised 3rds, Ds/Fs/Gs color tones) instead of recycling
 * the insane-tier track a 5th time. */
static const uint16_t demon_melody[32] = {
    N_D5, N_F5, N_A5, N_D6, N_F5, N_A5, N_C6, N_F5, N_A5, N_C6, N_E6, N_A5, N_G5, N_B5, N_D6, N_G5,
    N_D5, N_Fs5, N_A5, N_D6, N_B4, N_D5, N_Fs5, N_B4, N_F5, N_Gs5, N_A5, N_Ds6, N_G5, N_Ds5, N_B5, N_G5};
static const uint16_t demon_bass[8] = {N_D3, N_F3, N_A3, N_G3, N_D3, N_B2, N_F3, N_G3};

static const Track tracks[TIER_COUNT] = {
    {easy_melody, easy_bass, 32, 140},
    {med_melody, med_bass, 32, 115},
    {hard_melody, hard_bass, 48, 90},
    {demon_melody, demon_bass, 32, 85},
};

static const Note sfx_jump[] = {{900, 12}, {1400, 18}};
static const Note sfx_death[] = {{900, 30}, {700, 30}, {520, 35}, {380, 40}, {260, 50}, {150, 70}};
static const Note sfx_coin[] = {{1600, 40}, {2000, 60}};
static const Note sfx_checkpoint[] = {{900, 50}, {1400, 70}};
static const Note sfx_complete[] = {{523, 80}, {659, 80}, {784, 80}, {1047, 80}, {1319, 240}};
static const Note sfx_move[] = {{1800, 12}};
static const Note sfx_select[] = {{1800, 15}, {0, 15}, {1800, 15}};
static const Note sfx_insane[] = {{300, 120}, {250, 120}, {200, 160}};
/* Boot chime: quick rising C-major triad into the octave, GD-menu-ding shaped */
static const Note sfx_boot[] = {{N_C5, 70}, {N_E5, 70}, {N_G5, 70}, {N_C6, 160}};

typedef struct {
    const Note* notes;
    uint8_t count;
} Sfx;

#define SFX_ENTRY(a) {a, sizeof(a) / sizeof(a[0])}
static const Sfx sfx_table[SFX_COUNT] = {
    SFX_ENTRY(sfx_jump),   SFX_ENTRY(sfx_death),  SFX_ENTRY(sfx_coin),   SFX_ENTRY(sfx_checkpoint),
    SFX_ENTRY(sfx_complete), SFX_ENTRY(sfx_move), SFX_ENTRY(sfx_select), SFX_ENTRY(sfx_insane),
    SFX_ENTRY(sfx_boot),
};

static uint16_t music_hz(const Track* t, uint32_t start, uint32_t now) {
    const uint32_t loop_ms = (uint32_t)t->steps * t->step_ms;
    const uint32_t pos = (now - start) % loop_ms;
    const uint32_t step = pos / t->step_ms;
    const uint32_t into = pos % t->step_ms;
    const uint16_t mel = t->melody[step];
    if(mel == 0) return 0;
    if(into * 100u >= (uint32_t)t->step_ms * (100u - GAP_PERCENT)) return 0;
    if(step % BEATS_PER_BASS == 0 && into < BASS_HIT_MS) return t->bass[step / BEATS_PER_BASS];
    return mel;
}

static int32_t audio_thread(void* context) {
    Audio* a = context;
    a->has_speaker = furi_hal_speaker_acquire(SPEAKER_ACQUIRE_MS);
    float last_hz = 0.0f;
    bool tone_on = false;
    const Sfx* sfx = NULL;
    uint8_t sfx_idx = 0;
    uint32_t sfx_end = 0;
    while(!a->stop) {
        const uint32_t now = furi_get_tick();
        const int8_t pending = a->sfx_pending;
        if(pending > 0 && pending <= SFX_COUNT) {
            sfx = &sfx_table[pending - 1];
            a->sfx_pending = 0;
            sfx_idx = 0;
            sfx_end = now + sfx->notes[0].ms;
        }
        if(sfx != NULL && (int32_t)(now - sfx_end) >= 0) {
            sfx_idx++;
            if(sfx_idx >= sfx->count) {
                sfx = NULL;
            } else {
                sfx_end = now + sfx->notes[sfx_idx].ms;
            }
        }
        uint16_t hz = 0;
        if(sfx != NULL) {
            hz = sfx->notes[sfx_idx].hz;
        } else {
            /* read once: the main thread can set NO_TIER at any moment, and tracks[-1] would fault */
            const int8_t tier = a->tier;
            if(tier >= 0 && tier < TIER_COUNT) hz = music_hz(&tracks[tier], a->music_start, now);
        }
        float out = (float)hz;
        if(a->shifted) out *= INSANE_PITCH_MULT;
        const bool audible = hz >= MIN_HZ && out <= MAX_HZ && !a->muted && a->has_speaker;
        if(audible) {
            if(!tone_on || out != last_hz) {
                furi_hal_speaker_start(out, (float)a->volume / 100.0f);
                last_hz = out;
                tone_on = true;
            }
        } else if(tone_on) {
            furi_hal_speaker_stop();
            tone_on = false;
        }
        furi_delay_ms(AUDIO_POLL_MS);
    }
    if(a->has_speaker) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        a->has_speaker = false;
    }
    return 0;
}

Audio* audio_alloc(void) {
    Audio* a = malloc(sizeof(Audio));
    a->stop = false;
    a->muted = false;
    a->shifted = false;
    a->volume = DEFAULT_VOLUME;
    a->tier = NO_TIER;
    a->music_start = 0;
    a->sfx_pending = 0;
    a->has_speaker = false; /* set by the audio thread, which owns the speaker */
    a->thread = furi_thread_alloc_ex("GeoDashAudio", AUDIO_STACK, audio_thread, a);
    furi_thread_start(a->thread);
    return a;
}

void audio_free(Audio* audio) {
    if(audio == NULL) return;
    audio->stop = true;
    furi_thread_join(audio->thread);
    furi_thread_free(audio->thread);
    free(audio);
}

void audio_start_music(Audio* audio, uint8_t level_id) {
    uint8_t tier = level_get(level_id)->music_tier;
    if(tier < 1 || tier > TIER_COUNT) tier = 1;
    audio->music_start = furi_get_tick();
    audio->tier = (int8_t)(tier - 1);
}

void audio_stop_music(Audio* audio) {
    audio->tier = NO_TIER;
}

void audio_play_sfx(Audio* audio, SfxId sfx) {
    if(sfx < SFX_COUNT) audio->sfx_pending = (int8_t)(sfx + 1);
}

void audio_set_volume(Audio* audio, uint8_t volume) {
    audio->volume = volume > 100 ? 100 : volume;
}

void audio_set_muted(Audio* audio, bool muted) {
    audio->muted = muted;
}

void audio_set_pitch_shift(Audio* audio, bool shifted) {
    audio->shifted = shifted;
}
