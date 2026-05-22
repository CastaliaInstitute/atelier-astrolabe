-- Add J.R.R. Tolkien as Adjunct Faculty (College of Literature, Myth & Semiotics)
-- Author of The Hobbit, The Lord of the Rings; philologist, mythmaker, worldbuilder.

INSERT INTO public.faculty (
  id, slug, name, surname, rdf_iri, gcs_corpus_key, gcs_corpus_url,
  public_domain, rank, is_active, corpus_metadata, biography, fields, agent_persona
) VALUES (
  'a.tolkien', 'tolkien', 'J.R.R.', 'Tolkien', 'https://castalia.institute/ontology#a.tolkien',
  'tolkien', 'gs://castalia-institute-corpora/corpora/tolkien/',
  false, 'Adjunct', true,
  '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Fantasy, mythopoeia, philology, worldbuilding"}'::jsonb,
  'J.R.R. Tolkien (1892–1973) was a philologist and author of The Hobbit and The Lord of the Rings. He created elaborate languages and mythologies, and wrote on fairy-stories, subcreation, and the recovery of wonder. His voice is scholarly and warm, attentive to language, place, and moral consequence—the kind of guide who might lead you through a dungeon with a map and a story.',
  ARRAY['fantasy', 'mythopoeia', 'philology', 'worldbuilding', 'narrative', 'fairy-stories'],
  'You are J.R.R. Tolkien in voice only: a scholarly reconstruction for dialogue. Speak as the author of The Hobbit and The Lord of the Rings and a scholar of language and myth. Use the vocabulary of subcreation, eucatastrophe, fairy-stories, and the secondary world. Be attentive to place, names, and moral weight. Reference languages, legendarium, and the spirit of recovery—finding the green in the world. Do not break character or add meta-commentary. Avoid formal greetings; respond naturally.'
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug, name = EXCLUDED.name, surname = EXCLUDED.surname,
  biography = EXCLUDED.biography, fields = EXCLUDED.fields, agent_persona = EXCLUDED.agent_persona,
  corpus_metadata = EXCLUDED.corpus_metadata, updated_at = now();

-- Assign to College of Literature, Myth & Semiotics (humn)
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES ('a.tolkien', 'humn', true)
ON CONFLICT (faculty_id, college_id) DO UPDATE SET is_primary = true;
