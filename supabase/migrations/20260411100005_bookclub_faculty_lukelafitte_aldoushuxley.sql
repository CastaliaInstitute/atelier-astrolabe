-- Faculty ids for book club hosts (Luke Lafitte, Aldous Huxley).
-- Migrates clubs from l.lafitte / legacy a.huxley if those ids were used.

INSERT INTO public.faculty (id, name, surname, slug, rdf_iri)
VALUES
  (
    'a.lukelafitte',
    'Luke',
    'Lafitte',
    'luke-lafitte',
    'https://castalia.institute/ontology#a.lukelafitte'
  ),
  (
    'a.AldousHuxley',
    'Aldous',
    'Huxley',
    'aldous-huxley',
    'https://castalia.institute/ontology#a.AldousHuxley'
  )
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  slug = EXCLUDED.slug,
  rdf_iri = EXCLUDED.rdf_iri;

UPDATE public.book_clubs
SET host_faculty_id = 'a.lukelafitte'
WHERE id = 'machine-intelligence-imaginal'
  AND host_faculty_id = 'l.lafitte';

UPDATE public.book_clubs
SET host_faculty_id = 'a.AldousHuxley'
WHERE id = 'perennial-philosophy'
  AND host_faculty_id = 'a.huxley';
