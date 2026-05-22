-- Add missing faculty for Issue 2 articles
-- These faculty members are needed for article generation and reviews

-- a.chizhevsky (Alexander Chizhevsky) - Solar Cycles and Human Behavior
INSERT INTO public.faculty (id, name, surname, biography, is_active, rank)
VALUES (
  'a.chizhevsky',
  'Alexander',
  'Chizhevsky',
  'Russian biophysicist and space scientist known for his work on heliobiology and the effects of solar activity on human behavior and biological systems.',
  true,
  'Adjunct'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  biography = EXCLUDED.biography,
  is_active = EXCLUDED.is_active,
  rank = EXCLUDED.rank;

-- a.schmitt (Carl Schmitt) - Default heretic reviewer
INSERT INTO public.faculty (id, name, surname, biography, is_active, rank)
VALUES (
  'a.schmitt',
  'Carl',
  'Schmitt',
  'German jurist, political theorist, and philosopher known for his critique of liberalism and his concept of the political. His work challenges settled assumptions about law, sovereignty, and the state.',
  true,
  'Adjunct'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  biography = EXCLUDED.biography,
  is_active = EXCLUDED.is_active,
  rank = EXCLUDED.rank;

-- a.watts (Alan Watts) - Persona article
INSERT INTO public.faculty (id, name, surname, biography, is_active, rank)
VALUES (
  'a.watts',
  'Alan',
  'Watts',
  'British-American philosopher, writer, and speaker known for interpreting and popularizing Eastern philosophy for Western audiences. His work explores the nature of consciousness, identity, and the self.',
  true,
  'Adjunct'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  biography = EXCLUDED.biography,
  is_active = EXCLUDED.is_active,
  rank = EXCLUDED.rank;

-- a.smith (Adam Smith) - Trust, Institutions article
INSERT INTO public.faculty (id, name, surname, biography, is_active, rank)
VALUES (
  'a.smith',
  'Adam',
  'Smith',
  'Scottish economist and philosopher, a key figure in the Scottish Enlightenment. Known for his work on moral philosophy and political economy, particularly "The Wealth of Nations" and "The Theory of Moral Sentiments".',
  true,
  'Seated'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  biography = EXCLUDED.biography,
  is_active = EXCLUDED.is_active,
  rank = EXCLUDED.rank;

-- a.poincare (Henri Poincaré) - Model Rightness article
INSERT INTO public.faculty (id, name, surname, biography, is_active, rank)
VALUES (
  'a.poincare',
  'Henri',
  'Poincaré',
  'French mathematician, theoretical physicist, engineer, and philosopher of science. Known for his work on the three-body problem, topology, and the philosophy of mathematics and science.',
  true,
  'Seated'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  biography = EXCLUDED.biography,
  is_active = EXCLUDED.is_active,
  rank = EXCLUDED.rank;

-- Assign colleges (if faculty_colleges table exists)
-- Note: Adjust college assignments based on your schema
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES
  ('a.chizhevsky', 'natp', true),
  ('a.schmitt', 'soci', true),
  ('a.watts', 'humn', true),
  ('a.smith', 'soci', true),
  ('a.poincare', 'math', true)
ON CONFLICT (faculty_id, college_id) DO NOTHING;
