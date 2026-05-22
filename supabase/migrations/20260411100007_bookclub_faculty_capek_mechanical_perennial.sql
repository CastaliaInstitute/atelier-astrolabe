-- Mechanical Men led by faculty a.Capek (ASCII id; display surname remains Čapek).
-- Perennial Philosophy led by a.AldousHuxley.

INSERT INTO public.faculty (id, name, surname, slug, rdf_iri)
VALUES
  (
    'a.Capek',
    'Karel',
    'Čapek',
    'karel-capek',
    'https://castalia.institute/ontology#a.Capek'
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
SET host_faculty_id = 'a.Capek'
WHERE id = 'mechanical-men';

UPDATE public.book_clubs
SET host_faculty_id = 'a.AldousHuxley'
WHERE id = 'perennial-philosophy';

-- If you previously used a.Čapek or a.capek, repoint any stray rows:
UPDATE public.book_clubs
SET host_faculty_id = 'a.Capek'
WHERE id = 'mechanical-men'
  AND host_faculty_id IN ('a.capek', 'a.Čapek');

-- Legacy id a.capek may still exist for other Castalia properties; remove manually if unused:
-- DELETE FROM public.faculty WHERE id = 'a.capek';
