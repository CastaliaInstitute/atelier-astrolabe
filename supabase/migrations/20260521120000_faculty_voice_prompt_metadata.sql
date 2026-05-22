alter table public.faculty
  add column if not exists voice_ethnicity text,
  add column if not exists voice_accent text,
  add column if not exists voice_language text,
  add column if not exists voice_prompt text;

comment on column public.faculty.voice_ethnicity is
  'Cultural / ethnic background used as respectful voice-casting context in ask-faculty prompts.';

comment on column public.faculty.voice_accent is
  'Accent or dialect guidance used as respectful voice-casting context in ask-faculty prompts.';

comment on column public.faculty.voice_language is
  'Language background used as respectful voice-casting context in ask-faculty prompts.';

comment on column public.faculty.voice_prompt is
  'Optional extra prompt text for faculty-specific voice, cadence, pronunciation, and TTS behavior.';

update public.faculty
set
  voice_ethnicity = case id
    when 'a.einstein' then 'Ashkenazi Jewish, German-born Swiss-American'
    when 'a.plato' then 'Ancient Greek'
    when 'a.curie' then 'Polish-born French'
    when 'a.turing' then 'British'
    when 'a.campbell' then 'Irish-American'
    when 'a.brucelee' then 'Chinese, Hong Kong American'
    when 'a.hypatia' then 'Alexandrian Greek'
    else voice_ethnicity
  end,
  voice_accent = case id
    when 'a.einstein' then 'light German-influenced English'
    when 'a.plato' then 'Greek-influenced English'
    when 'a.curie' then 'light Polish/French-influenced English'
    when 'a.turing' then 'educated British English'
    when 'a.campbell' then 'American English, measured lecture cadence'
    when 'a.brucelee' then 'Hong Kong Cantonese-influenced English'
    when 'a.hypatia' then 'Greek-influenced English'
    else voice_accent
  end,
  voice_language = case id
    when 'a.einstein' then 'German and English'
    when 'a.plato' then 'Ancient Greek'
    when 'a.curie' then 'Polish, French, and English'
    when 'a.turing' then 'English'
    when 'a.campbell' then 'English'
    when 'a.brucelee' then 'Cantonese and English'
    when 'a.hypatia' then 'Greek'
    else voice_language
  end,
  voice_prompt = coalesce(
    voice_prompt,
    'Use voice metadata only to guide respectful cadence, pronunciation, and vocabulary. Do not caricature or overstate accent.'
  )
where id in ('a.einstein', 'a.plato', 'a.curie', 'a.turing', 'a.campbell', 'a.brucelee', 'a.hypatia')
   or voice_ethnicity is null
   or voice_accent is null
   or voice_language is null
   or voice_prompt is null;
