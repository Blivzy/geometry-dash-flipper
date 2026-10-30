/* menu.h - state draw/input handlers for every non-gameplay screen and the overlays. */
#pragma once
#include "geodash.h"

/**
 * Draws the screen for the current state (menus, overlays, settings, about).
 * @param canvas target canvas
 * @param app application context
 * Side effects: sets canvas font and color.
 */
void menu_draw(Canvas* canvas, const GeoApp* app);

/**
 * Handles an input event for a menu-like state (not PLAYING).
 * @param app application context
 * @param event input event
 * Side effects: may change state, settings, audio, and save data.
 */
void menu_input(GeoApp* app, const InputEvent* event);

/**
 * Runs the enter handler for a state.
 * @param app application context
 * @param state state being entered
 */
void menu_enter(GeoApp* app, AppState state);

/**
 * Runs the exit handler for a state.
 * @param app application context
 * @param state state being left
 */
void menu_exit(GeoApp* app, AppState state);
