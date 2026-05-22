-- Optional profile copy for the “funded pitch” block (defaults stay in app when null).
-- Bruce Lee agent: custom section titles (app defaults are short “Plans with support” / “Questions I'd explore next:”).

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS funded_section_title text,
  ADD COLUMN IF NOT EXISTS funded_questions_heading text;

COMMENT ON COLUMN public.faculty.funded_section_title IS 'Overrides the profile H2 above research_statement; null = app default ("Plans with support").';
COMMENT ON COLUMN public.faculty.funded_questions_heading IS 'Overrides the H3 above research_questions; null = app default ("Questions I''d explore next:").';

UPDATE public.faculty
SET
  funded_section_title = 'What I’d share with your support',
  funded_questions_heading = 'Lines of practice I’d take further:',
  research_statement = $stmt$
I write and teach from **primary sources and my own record**—film, interviews, and published notes on discipline, adaptation, and direct expression—without turning life into legend or putting words in my mouth.

With steady support I would:

- **Build honest syllabi** across embodied arts and classical texts, always pinned to cited passages.
- **Grade Ask-Faculty answers** for fidelity: reward what the corpus actually supports; penalize invented “voice.”
- **Keep the reader honest**—clear labels when someone is talking with a synthetic faculty agent versus reading documentary material.

That support is for editorial time, careful corpus work, and review—not spectacle or borrowed celebrity.
$stmt$,
  research_questions = ARRAY[
    'How do we teach adaptive, whole-body practice while every claim stays tied to retrievable sources?',
    'Which cues in a corpus best stop a faculty agent from confabulating quotes or tone?',
    'Where do Eastern praxis texts and Western performance science meet without hand-waving?'
  ]::text[]
WHERE id = 'a.brucelee';
