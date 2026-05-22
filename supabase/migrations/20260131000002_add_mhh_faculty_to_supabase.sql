-- ============================================================================
-- Add MHH Faculty to Supabase Faculty Table
-- ============================================================================
-- Ensures a.arendt, a.shannon, a.foucault-soci, and a.nabokov exist in the
-- faculty table (they should already exist from RDF sync, but this ensures
-- they're present with correct MHH metadata)
-- ============================================================================

-- Note: These faculty should already exist in the faculty table from RDF sync.
-- This migration ensures they exist and have the correct metadata for MHH.
-- If they don't exist, they'll be created with minimal required fields.

-- ============================================================================
-- 1. HANNAH ARENDT
-- ============================================================================
INSERT INTO public.faculty (
  id,
  slug,
  name,
  rdf_iri,
  surname,
  gcs_corpus_key,
  gcs_corpus_url,
  public_domain,
  rank,
  is_active,
  corpus_metadata
) VALUES (
  'a.arendt',
  'a-arendt',
  'Hannah Arendt',
  'https://inquiry.institute/ontology#a.arendt',
  'arendt',
  'arendt',
  'gs://inquiry-institute-corpora/corpora/arendt/',
  false, -- Died 1975, not yet public domain
  'Adjunct',
  true,
  '{"source": "mhh_arc", "role": "core", "focus": "Judgment, banality, responsibility"}'::jsonb
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug,
  name = EXCLUDED.name,
  rdf_iri = EXCLUDED.rdf_iri,
  surname = EXCLUDED.surname,
  gcs_corpus_key = COALESCE(faculty.gcs_corpus_key, EXCLUDED.gcs_corpus_key),
  gcs_corpus_url = COALESCE(faculty.gcs_corpus_url, EXCLUDED.gcs_corpus_url),
  public_domain = EXCLUDED.public_domain,
  rank = EXCLUDED.rank,
  corpus_metadata = COALESCE(faculty.corpus_metadata, '{}'::jsonb) || EXCLUDED.corpus_metadata,
  updated_at = now();

-- ============================================================================
-- 2. CLAUDE SHANNON
-- ============================================================================
INSERT INTO public.faculty (
  id,
  slug,
  name,
  rdf_iri,
  surname,
  gcs_corpus_key,
  gcs_corpus_url,
  public_domain,
  rank,
  is_active,
  corpus_metadata
) VALUES (
  'a.shannon',
  'a-shannon',
  'Claude Shannon',
  'https://inquiry.institute/ontology#a.shannon',
  'shannon',
  'shannon',
  'gs://inquiry-institute-corpora/corpora/shannon/',
  false, -- Died 2001, not yet public domain
  'Seated',
  true,
  '{"source": "mhh_arc", "role": "core", "focus": "Information vs meaning"}'::jsonb
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug,
  name = EXCLUDED.name,
  rdf_iri = EXCLUDED.rdf_iri,
  surname = EXCLUDED.surname,
  gcs_corpus_key = COALESCE(faculty.gcs_corpus_key, EXCLUDED.gcs_corpus_key),
  gcs_corpus_url = COALESCE(faculty.gcs_corpus_url, EXCLUDED.gcs_corpus_url),
  public_domain = EXCLUDED.public_domain,
  rank = EXCLUDED.rank,
  corpus_metadata = COALESCE(faculty.corpus_metadata, '{}'::jsonb) || EXCLUDED.corpus_metadata,
  updated_at = now();

-- ============================================================================
-- 3. MICHEL FOUCAULT (Sociology)
-- ============================================================================
INSERT INTO public.faculty (
  id,
  slug,
  name,
  rdf_iri,
  surname,
  gcs_corpus_key,
  gcs_corpus_url,
  public_domain,
  rank,
  is_active,
  corpus_metadata
) VALUES (
  'a.foucault-soci',
  'a-foucault-soci',
  'Michel Foucault',
  'https://inquiry.institute/ontology#a.foucault-soci',
  'foucault',
  'foucault',
  'gs://inquiry-institute-corpora/corpora/foucault/',
  false, -- Died 1984, not yet public domain
  'Adjunct',
  true,
  '{"source": "mhh_arc", "role": "core", "focus": "Classification, power, surveillance", "note": "Sociology variant"}'::jsonb
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug,
  name = EXCLUDED.name,
  rdf_iri = EXCLUDED.rdf_iri,
  surname = EXCLUDED.surname,
  gcs_corpus_key = COALESCE(faculty.gcs_corpus_key, EXCLUDED.gcs_corpus_key),
  gcs_corpus_url = COALESCE(faculty.gcs_corpus_url, EXCLUDED.gcs_corpus_url),
  public_domain = EXCLUDED.public_domain,
  rank = EXCLUDED.rank,
  corpus_metadata = COALESCE(faculty.corpus_metadata, '{}'::jsonb) || EXCLUDED.corpus_metadata,
  updated_at = now();

-- ============================================================================
-- 4. VLADIMIR NABOKOV
-- ============================================================================
INSERT INTO public.faculty (
  id,
  slug,
  name,
  rdf_iri,
  surname,
  gcs_corpus_key,
  gcs_corpus_url,
  public_domain,
  rank,
  is_active,
  corpus_metadata
) VALUES (
  'a.nabokov',
  'a-nabokov',
  'Vladimir Nabokov',
  'https://inquiry.institute/ontology#a.nabokov',
  'nabokov',
  'nabokov',
  'gs://inquiry-institute-corpora/corpora/nabokov/',
  false, -- Died 1977, not yet public domain
  'Adjunct',
  true,
  '{"source": "mhh_arc", "role": "guest", "focus": "Interpretation, misreading, Pale Fire"}'::jsonb
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug,
  name = EXCLUDED.name,
  rdf_iri = EXCLUDED.rdf_iri,
  surname = EXCLUDED.surname,
  gcs_corpus_key = COALESCE(faculty.gcs_corpus_key, EXCLUDED.gcs_corpus_key),
  gcs_corpus_url = COALESCE(faculty.gcs_corpus_url, EXCLUDED.gcs_corpus_url),
  public_domain = EXCLUDED.public_domain,
  rank = EXCLUDED.rank,
  corpus_metadata = COALESCE(faculty.corpus_metadata, '{}'::jsonb) || EXCLUDED.corpus_metadata,
  updated_at = now();

-- ============================================================================
-- VERIFICATION
-- ============================================================================
SELECT 
  id,
  slug,
  name,
  rank,
  is_active,
  corpus_metadata->>'role' as mhh_role,
  corpus_metadata->>'focus' as mhh_focus
FROM public.faculty
WHERE id IN ('a.arendt', 'a.shannon', 'a.foucault-soci', 'a.nabokov')
ORDER BY id;
