-- Corpus-tuned teaching voice: full system text + optional structured card (improve-voice script).
-- ask-faculty uses voice_prompt when set (highest priority before agent_persona / default build).

alter table public.faculty
  add column if not exists voice_prompt text,
  add column if not exists voice_card jsonb;

comment on column public.faculty.voice_prompt is
  'Corpus-tuned system prompt; when non-null/non-empty, ask-faculty uses it as the base (plus RAG/commonplace), ahead of agent_persona and the default reconstruction prompt.';

comment on column public.faculty.voice_card is
  'Optional JSON from improve-voice (lexical markers, rubric fields, etc.); for tooling and display, not injected into the LLM by default.';
