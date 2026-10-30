# GeoDash 1.6 - unofficial fan demake for Flipper Zero, made by blivzy
Not affiliated with RobTop Games. Target: ufbt release channel (written against firmware ~0.103 / API ~87; NOT verified, see below).

## Build
`./build.sh` (installs ufbt, `ufbt update`, `ufbt`) -> `dist/geodash.fap`. CI: `.github/workflows/build.yml` uploads the .fap artifact.
Place the folder at `applications_user/geodash/` if building inside a firmware tree (`./fbt fap_geodash`).

## Install
Copy `geodash.fap` to `SD:/apps/Games/`, then Apps -> Games -> GeoDash.

## Controls
| Where | Keys |
|---|---|
| Menus | Up/Down move, Ok select, Back = back, Back (hold) = main menu |
| Level select | Right/Left toggles Practice, Ok starts |
| Playing (both orientations) | Up **or** Ok = jump / thrust / orb / flip; Left (practice only) = drop a checkpoint at the player's current position; Back = pause; Back (hold) = main menu |
| Main menu easter egg | hold Left, press Ok -> INSANE MODE (x1.5 speed, halved coyote/buffer, +2 semitones; not saved; insane/practice runs never change best %) |

Orientation: the GUI layer already remaps D-pad keys for a rotated ViewPort, so Up and Ok both mean "jump" in Horizontal and Vertical. Vertical is applied only while PLAYING (menus stay horizontal so text fits); it is chosen in Settings (1 change/second limit).
Holding the key keeps jumping (cube), flips ball only on a fresh press, and triggers orbs.

## Design notes
- Physics: fixed step on the main thread, paced by the input-queue timeout (no timer thread). Settings -> Speed picks the step: Normal 22 ms (~45 Hz, default), Slow 28 ms, Fast 16 ms (the old 1.0-1.3 pace). The draw callback copies g_world under one FuriMutex and draws from the copy; SD writes and orientation changes run outside the mutex.
- Audio: own FuriThread, wall-clock music position (SFX borrowing resumes at correct offset), melody/bass alternate every 8 ms.
- Levels: generated once by `tools/gen_levels.py` from a library of hand-tuned patterns into const arrays (lengths 1189-2617 cells), streamed through a 256-object window.
- Save: 14-byte raw struct, version-checked, clamped. Best % only from normal (non-practice, non-insane) runs.

## Deviations from the brief (honest list)
- Tilt/IMU: not implemented (hidden; base Flipper has no IMU).
- FontSmall and canvas_draw_icon/draw_triangle are not used (not verified; triangles are drawn with lines).
- Pink orbs/pads are implemented in the engine but no shipped level uses them (an unverified fake-out pattern could strand players on the ceiling).
- Levels are pattern-generated, not individually hand-laid; levels 7-8 are not "near-impossible".
- Vertical orientation only during play (see above).

## What 1.4 changed

Performance and feel, gameplay patterns untouched (`trace.c` hash identical on all 9 levels). Dropped timer ticks no longer queue up and fast-forward the game; the draw callback no longer holds the physics mutex while drawing; SD saves no longer run under it; audio thread race (music tier read twice) and inaudible sub-100 Hz bass notes fixed; new Speed setting; spikes get a halo so they read at speed; per-level palette inversion; horizon dither; new music loops. Not verified on hardware.

## What 1.1-1.3 changed
Rendering, menus, HUD and cosmetic particles only. `game_tick` physics, `level_data.c`, audio and the save format are untouched:
`tools/host_tests/trace.c` hashes 960,000 scripted ticks (all 8 levels x normal/practice/insane); original and 1.2 produce the same hash for levels 1-8 (`-DMAXLV=8`). 1.2 adds wave mode and level 9; `tools/host_tests/wave.c` checks all wave corridors.
Per-level looks live in `theme.c` (pure data). Preview any screen without hardware: `tools/host_render/build.sh out` (needs gcc + python3 + Pillow) writes `out.png`.

## Verification status
- Real `ufbt` build: **NOT done** (sandbox had no network). Run `./build.sh`.
- Host check: every .c passes `gcc -std=c11 -Wall -Wextra -fsyntax-only` against hand-written stub headers (`tools/host_stubs`), which only proves internal consistency, not SDK signatures.
- 1.1: all sources pass `-Wall -Wextra -fsyntax-only` with zero warnings; physics hash identical to 1.0; bot still completes levels 1-3; ufo/pat brute-force output identical to 1.0; every screen rendered through a host software canvas (horizontal and 64x128 vertical).
- Host physics tests (`tools/host_tests`, real game_core.c): a simple bot completes levels 1-3 end to end (levels 2-3 include ship/ball sections); every cube pattern and UFO pattern was brute-force checked to have a solution. Levels 4-8 were **not** completed end to end (bot too crude); they are untested.
- Hardware: not run.

## SDK call audit (unverified against headers - confirm on first build)
| Call | Expected header |
|---|---|
| furi_thread_alloc_ex/start/join/free, furi_delay_ms, furi_get_tick, furi_ms_to_ticks | furi/core/thread.h, kernel.h |
| furi_timer_alloc/start/stop/free | furi/core/timer.h |
| furi_mutex_*, furi_message_queue_* | furi/core/mutex.h, message_queue.h |
| furi_hal_speaker_acquire(uint32_t)->bool, _start(float,float), _stop, _release | furi_hal_speaker.h |
| view_port_*, gui_add/remove_view_port, canvas_* | gui/view_port.h, gui/gui.h, gui/canvas.h |
| storage_common_mkdir, storage_dir_exists, storage_file_* | storage/storage.h |
| notification_message, sequence_single/double_vibro, message_vibro_on/off, message_delay_50 | notification/notification_messages.h |

## Acceptance checklist
- [ ] ufbt build produces geodash.fap with zero warnings
- [ ] installs to SD:/apps/Games/ and appears in Apps -> Games
- [ ] launches without watchdog reset
- [ ] all 8 levels load and play 10 s
- [ ] death: SFX + vibration + shake + death screen
- [ ] one level completed end to end
- [ ] orientation setting works and persists
- [ ] mute silences all audio
- [ ] insane easter egg activates
- [ ] Back from every menu and gameplay exits cleanly
- [ ] SD removed mid-session: no crash
- [ ] 10 play/die/menu cycles: free heap stable

## Cleanup order on exit
timer stop+free -> audio thread join, speaker stop+release -> save -> ViewPort disabled, removed, freed -> records closed -> queue, mutex, app freed.
