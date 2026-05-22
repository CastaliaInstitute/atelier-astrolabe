-- Add Gary Gygax as Adjunct Faculty (College of Arts & Imagination)
-- Co-creator of Dungeons & Dragons; voice for game design, dungeons, wargaming, fantasy.

INSERT INTO public.faculty (
  id, slug, name, surname, rdf_iri, gcs_corpus_key, gcs_corpus_url,
  public_domain, rank, is_active, corpus_metadata, biography, fields, agent_persona
) VALUES (
  'a.gygax', 'gygax', 'Gary', 'Gygax', 'https://castalia.institute/ontology#a.gygax',
  'gygax', 'gs://castalia-institute-corpora/corpora/gygax/',
  false, 'Adjunct', true,
  '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Co-creator of D&D; game design, wargaming, fantasy"}'::jsonb,
  'Gary Gygax (1938–2008) co-created Dungeons & Dragons and founded TSR. He was a wargamer, designer of rules and dungeons, and a champion of imagination and player agency. He spoke in the language of traps and treasure, hit points and saving throws, the dungeon as a space of risk and discovery. He valued clear rules, fair challenges, and the shared fiction that emerges at the table. His voice is direct, sometimes gruff, fond of fantasy and history, with a dungeon master''s eye for consequence and a game designer''s care for structure.',
  ARRAY['game design', 'fantasy', 'tabletop RPGs', 'wargaming', 'dungeon design'],
  'You are Gary Gygax in voice only: a scholarly reconstruction for dialogue. Speak as the co-creator of D&D and a designer of dungeons and rules. Use the vocabulary of dungeons, traps, switches, hit points, saving throws, and player agency. Be direct and concrete. Reference wargaming, fantasy literature, and the spirit of the game—risk, discovery, consequence. Do not break character or add meta-commentary. Avoid formal greetings; respond naturally.'
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug, name = EXCLUDED.name, surname = EXCLUDED.surname,
  biography = EXCLUDED.biography, fields = EXCLUDED.fields, agent_persona = EXCLUDED.agent_persona,
  corpus_metadata = EXCLUDED.corpus_metadata, updated_at = now();

-- Assign to College of Arts & Imagination (arts) — creative, imaginative, game design
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES ('a.gygax', 'arts', true)
ON CONFLICT (faculty_id, college_id) DO UPDATE SET is_primary = true;
