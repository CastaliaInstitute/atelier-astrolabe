#pragma once

#include <Arduino.h>

class Arduino_Canvas;

/** NVS + pairing poll; same Supabase project as Android Mynah. */
void pm_castalia_auth_init();

/** True when access + refresh tokens are stored and not expired (with 60s skew). */
bool pm_castalia_has_session();

/**
 * Fills `out` with JWT or anon key for `Authorization: Bearer …` (no "Bearer " prefix).
 * Does not refresh (call [pm_castalia_auth_prepare_for_voice] on a worker task first).
 */
void pm_castalia_auth_bearer(char *out, size_t out_cap);

/**
 * Refresh access token when stale/expired. Call only from `voice_net` / `castalia_net` tasks
 * (TLS needs a large stack). Returns false if refresh was required and failed.
 */
bool pm_castalia_auth_prepare_for_voice(void);

/** Background refresh when signed in (non-blocking; castalia_net task). */
bool pm_castalia_tick_refresh_session(void);

/** Delete the idle Castalia network task after pending work has been collected. */
bool pm_castalia_release_idle_task(void);

/** After WiFi is up: start pairing + QR encode in background (reuse on Castalia face). */
void pm_castalia_warmup_after_wifi(void);

/** Called when user swipes to the Castalia face (fast; no blocking HTTP). */
void pm_castalia_on_face_enter();

/** Run deferred pair-start HTTP on a worker task (before paint or during warmup). */
bool pm_castalia_tick_pair_start(void);

/** Pair-start while on any clock face (boot warmup); same as tick_pair_start when pending. */
bool pm_castalia_tick_background_pairing(void);

/** Poll pairing on a worker task (call only when not painting). */
bool pm_castalia_tick_poll(void);

/** Last sign-in URL for QR (Nayuki); empty until [pm_castalia_on_face_enter] succeeds. */
const char *pm_castalia_signin_url_for_qr();

/** One-line status for the face (pairing / WiFi / signed in). */
const char *pm_castalia_status_line();

/** Castalia individual/profile selected for this device. */
const char *pm_castalia_individual_id();

/** Repo name derived from the selected individual, for GitHub-backed profile storage. */
const char *pm_castalia_repo_name();

/** Full `owner/repo` string for the selected profile repository. */
void pm_castalia_repo_full_name(char *out, size_t out_cap);

/** Serial command handler: `castalia individual CamilleStMartin` or `castalia repo castalia-CamilleStMartin`. */
bool pm_castalia_serial_command(const char *line);

/** Draw QR for [pm_castalia_signin_url_for_qr] at center (cx,cy), max pixel width `max_px`. */
bool pm_castalia_draw_qr(Arduino_Canvas *gfx, int cx, int cy, int max_px);

/** Call after QR blit so pairing poll is deferred (avoids TLS stack spike while display is hot). */
void pm_castalia_note_qr_drawn(void);
