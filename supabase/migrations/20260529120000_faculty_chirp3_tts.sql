-- Castalia live TTS: Google Chirp 3 HD voices + per-faculty delivery prompts.
-- Canonical migration lives in castalia.institute: 20260529120000_faculty_chirp3_tts.sql

alter table public.faculty
  add column if not exists google_tts_voice_name text,
  add column if not exists google_tts_language_code text,
  add column if not exists google_tts_prompt text;
