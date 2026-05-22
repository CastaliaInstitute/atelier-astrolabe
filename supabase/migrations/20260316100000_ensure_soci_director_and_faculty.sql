-- Ensure College of Social Inquiry has a director (a.adamsmith) and that the faculty row exists.
-- Run after 20260315200000; fixes cases where board had no soci row or a.adamsmith missing from faculty.

-- 1) Ensure a.adamsmith exists in faculty (in case 007 was never run on this DB)
INSERT INTO public.faculty (
  id, slug, name, surname, rdf_iri, gcs_corpus_key, gcs_corpus_url, corpus_metadata, is_active, public_domain
)
SELECT
  'a.adamsmith',
  'adamsmith',
  'Adam',
  'Smith',
  'https://castalia.institute/ontology#a.adamsmith',
  'smith',
  'gs://castalia-institute-corpora/corpora/smith/',
  '{"source": "project_gutenberg", "works_count": 4}'::jsonb,
  true,
  true
WHERE NOT EXISTS (SELECT 1 FROM public.faculty WHERE id = 'a.adamsmith');

-- 2) Set SOC director to a.adamsmith (match any existing soci row)
UPDATE public.board_of_directors
SET faculty_id = 'a.adamsmith'
WHERE college_id = 'soci' AND position_type = 'college';

-- 3) If no row for soci exists, insert one
INSERT INTO public.board_of_directors (faculty_id, college_id, position_type)
SELECT 'a.adamsmith', 'soci', 'college'
WHERE NOT EXISTS (
  SELECT 1 FROM public.board_of_directors
  WHERE college_id = 'soci' AND position_type = 'college'
);

-- 4) Ensure a.adamsmith is in faculty_colleges for soci
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
SELECT 'a.adamsmith', 'soci', true
WHERE EXISTS (SELECT 1 FROM public.faculty WHERE id = 'a.adamsmith')
  AND NOT EXISTS (SELECT 1 FROM public.faculty_colleges WHERE faculty_id = 'a.adamsmith' AND college_id = 'soci')
ON CONFLICT (faculty_id, college_id) DO UPDATE SET is_primary = true;
