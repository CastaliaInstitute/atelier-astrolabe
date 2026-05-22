create table if not exists public.faculty_member_history (
  id bigserial primary key,
  created_at timestamptz not null default now(),
  auth_user_id uuid,
  faculty_id text,
  faculty_slug text not null,
  faculty_name text,
  route text not null default 'ask-faculty',
  transcript text,
  reply text,
  faculty_voice jsonb not null default '{}'::jsonb,
  faculty_bust_url text
);

create index if not exists faculty_member_history_created_idx
  on public.faculty_member_history (created_at desc);

create index if not exists faculty_member_history_faculty_slug_idx
  on public.faculty_member_history (faculty_slug, created_at desc);

create index if not exists faculty_member_history_auth_user_idx
  on public.faculty_member_history (auth_user_id, created_at desc)
  where auth_user_id is not null;

insert into public.faculty (
  id,
  slug,
  name,
  rdf_iri,
  voice_accent,
  voice_language,
  voice_prompt,
  voice_card,
  google_tts_voice_name,
  google_tts_language_code
) values (
  'a.shakespeare',
  'a.shakespeare',
  'Shakespeare, William',
  'https://www.wikidata.org/wiki/Q692',
  'educated English, theatrical but natural',
  'Early Modern English and English',
  'Use a literate, warm, stage-trained cadence with light Elizabethan color. Keep modern clarity; do not overdo archaic phrasing.',
  jsonb_build_object(
    'ethnicity', 'Elizabethan English',
    'accent', 'educated English, theatrical but natural',
    'language', 'Early Modern English and English',
    'prompt', 'Use a literate, warm, stage-trained cadence with light Elizabethan color. Keep modern clarity; do not overdo archaic phrasing.',
    'googleTts', jsonb_build_object('languageCode', 'en-GB', 'name', 'en-GB-Neural2-B')
  ),
  'en-GB-Neural2-B',
  'en-GB'
) on conflict (id) do update set
  slug = excluded.slug,
  name = excluded.name,
  rdf_iri = excluded.rdf_iri,
  voice_accent = excluded.voice_accent,
  voice_language = excluded.voice_language,
  voice_prompt = excluded.voice_prompt,
  voice_card = coalesce(public.faculty.voice_card, '{}'::jsonb) || excluded.voice_card,
  google_tts_voice_name = excluded.google_tts_voice_name,
  google_tts_language_code = excluded.google_tts_language_code,
  updated_at = now();
