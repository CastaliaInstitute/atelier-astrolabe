alter table public.faculty
  add column if not exists google_tts_voice_name text,
  add column if not exists google_tts_language_code text;

comment on column public.faculty.google_tts_voice_name is
  'Google Cloud Text-to-Speech voice name used when this faculty member speaks on Astrolabe / Castalia voice surfaces.';

comment on column public.faculty.google_tts_language_code is
  'Google Cloud Text-to-Speech language code paired with google_tts_voice_name.';

update public.faculty
set
  google_tts_voice_name = case id
    when 'a.einstein' then 'en-US-Neural2-D'
    when 'a.plato' then 'en-GB-Neural2-B'
    when 'a.curie' then 'en-US-Neural2-F'
    when 'a.turing' then 'en-GB-Neural2-D'
    when 'a.campbell' then 'en-US-Neural2-J'
    when 'a.brucelee' then 'en-US-Neural2-I'
    else coalesce(google_tts_voice_name, 'en-GB-Neural2-A')
  end,
  google_tts_language_code = case id
    when 'a.plato' then 'en-GB'
    when 'a.turing' then 'en-GB'
    else coalesce(google_tts_language_code, 'en-US')
  end
where id in ('a.einstein', 'a.plato', 'a.curie', 'a.turing', 'a.campbell', 'a.brucelee')
   or google_tts_voice_name is null
   or google_tts_language_code is null;
