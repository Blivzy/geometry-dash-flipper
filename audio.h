/* audio.h - piezo sequencer API: music loops, SFX, mute and volume. */
#pragma once
#include "geodash.h"

/**
 * Allocates the audio engine and starts its thread. Tries to acquire the speaker;
 * if that fails the engine runs muted-silent and never errors.
 * @return audio handle (never NULL)
 */
Audio* audio_alloc(void);

/**
 * Stops the thread, silences and releases the speaker, frees the handle.
 * @param audio handle from audio_alloc (NULL tolerated)
 */
void audio_free(Audio* audio);

/**
 * Starts the looping music for a level's tier.
 * @param audio handle
 * @param level_id 0..LEVEL_COUNT-1
 */
void audio_start_music(Audio* audio, uint8_t level_id);

/**
 * Stops the music (SFX still play).
 * @param audio handle
 */
void audio_stop_music(Audio* audio);

/**
 * Queues a sound effect; it borrows the beeper and music resumes at the right offset.
 * @param audio handle
 * @param sfx effect id
 */
void audio_play_sfx(Audio* audio, SfxId sfx);

/**
 * Sets volume.
 * @param audio handle
 * @param volume 0..100 (clamped)
 */
void audio_set_volume(Audio* audio, uint8_t volume);

/**
 * Mutes or unmutes. While muted the sequencer still advances time.
 * @param audio handle
 * @param muted true to silence
 */
void audio_set_muted(Audio* audio, bool muted);

/**
 * Enables the +2 semitone insane pitch shift.
 * @param audio handle
 * @param shifted true to shift
 */
void audio_set_pitch_shift(Audio* audio, bool shifted);
