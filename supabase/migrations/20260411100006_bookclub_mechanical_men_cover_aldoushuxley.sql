-- Mechanical Men: cover art (served from /covers/mechanical-men.png on the bookclub host).
-- Perennial Philosophy: faculty id a.AldousHuxley; drop legacy a.aldoushuxley row if unused.

UPDATE public.book_clubs
SET cover_image_url = '/covers/mechanical-men.png'
WHERE id = 'mechanical-men';

INSERT INTO public.faculty (id, name, surname, slug, rdf_iri)
VALUES (
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
SET
  host_faculty_id = 'a.AldousHuxley',
  description = 'The Perennial Philosophy reading group (one Matrix space for the series): primary texts Huxley drew upon for his 1945 anthology — Vedanta, Buddhism, Christian mysticism, and Taoist sources — then The Perennial Philosophy as capstone. Led by faculty persona a.AldousHuxley.'
WHERE id = 'perennial-philosophy';

DELETE FROM public.faculty
WHERE id = 'a.aldoushuxley';
