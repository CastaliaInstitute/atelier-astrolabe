#pragma once

/*
 * Shared Astrolabe baseline contracts for face/sensor-derived reflection.
 *
 * These strings are intentionally conservative. Facial and biometric signals are
 * device observations for mindfulness prompts, not evidence of identity,
 * diagnosis, truthfulness, personality, intent, or stable mental state.
 */

#define ASTROLABE_FACE_METRICS_URL_DEFAULT "https://face-api.castalia.institute/v1/face"
#define ASTROLABE_FACE_METRICS_SOURCE_LABEL "face-api.castalia.institute"

#define ASTROLABE_MINDFULNESS_INTENDED_USE "self-reflection and mindfulness"

#define ASTROLABE_MINDFULNESS_POLICY_TEXT \
    "Use face and sensor metrics only as uncertain observable cues for self-reflection and mindfulness. " \
    "Do not infer identity, personality, truthfulness, diagnosis, intent, or stable mental state. " \
    "The user remains the authority on their internal state. Use cues as invitations for self-observation, " \
    "not evaluations."

#define ASTROLABE_MINDFULNESS_RESPONSE_STYLE_TEXT \
    "Use concise, non-judgmental language. Prefer gentle check-ins such as what do you notice, " \
    "would you like to take a breath, or there may be some visible tension."
