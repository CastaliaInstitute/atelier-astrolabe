-- Extend voice usage events from TTS-only attribution to full STT/LLM/TTS metering.
-- Canonical migration should also live in castalia.institute.

alter table if exists public.voice_usage_events
  add column if not exists model text,
  add column if not exists audio_seconds numeric(12, 3),
  add column if not exists input_tokens int not null default 0,
  add column if not exists output_tokens int not null default 0;

comment on column public.voice_usage_events.model is
  'Provider model identifier when the event is LLM/STT/TTS model-specific.';

comment on column public.voice_usage_events.audio_seconds is
  'Billable input audio seconds for STT or realtime audio events.';

comment on column public.voice_usage_events.input_tokens is
  'Estimated or provider-reported input tokens for LLM events.';

comment on column public.voice_usage_events.output_tokens is
  'Estimated or provider-reported output tokens for LLM events.';

create index if not exists idx_voice_usage_events_service_created
  on public.voice_usage_events (service, created_at desc);
