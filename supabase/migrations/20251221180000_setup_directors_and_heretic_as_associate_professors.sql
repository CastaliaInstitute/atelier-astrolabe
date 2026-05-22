-- Setup directors + aHeretic as Associate Professors
-- Idempotent: safe to run multiple times.
--
-- This migration is intentionally defensive because the repository contains
-- multiple historical shapes of the `public.faculty` table. We:
-- 1) Ensure the `rank` column exists and allows 'Associate Professor'
-- 2) Ensure a few commonly-used columns exist (used by API/Edge functions)
-- 3) Upsert 10 directors + aHeretic with rank = 'Associate Professor'

-- ============================================================================
-- 1) Ensure columns exist (no-op if already present)
-- ============================================================================

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS rank text;

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS surname text;

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS slug text;

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS public_domain boolean DEFAULT false;

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS fields text[];

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS portrait_url text;

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS is_active boolean DEFAULT true;

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS corpus_metadata jsonb DEFAULT '{}'::jsonb;

-- ============================================================================
-- 2) Ensure rank constraint allows "Associate Professor"
-- ============================================================================

DO $$
BEGIN
  IF EXISTS (
    SELECT 1
    FROM pg_constraint
    WHERE conname = 'faculty_rank_check'
      AND conrelid = 'public.faculty'::regclass
  ) THEN
    ALTER TABLE public.faculty DROP CONSTRAINT faculty_rank_check;
  END IF;

  -- Re-add with the full set used across the codebase/migrations.
  ALTER TABLE public.faculty
    ADD CONSTRAINT faculty_rank_check
    CHECK (rank IS NULL OR rank IN (
      'Adjunct',
      'Assistant Professor',
      'Associate Professor',
      'Professor',
      'Emeritus',
      'Seated'
    ));
END $$;

-- ============================================================================
-- 3) Upsert directors + aHeretic
-- ============================================================================
-- Notes:
-- - We set surname to empty string so UI displayName remains `name` (since JS
--   treats '' as falsy), while still satisfying schemas that may require NOT NULL.
-- - We keep `id` as the canonical dotted identifier used throughout the codebase.
-- - `slug` is set to a URL-friendly form (dots -> dashes) for convenience.
-- - We set public_domain=false (these are institutional/role agents).

DO $$
DECLARE
  use_gcs boolean;
  use_rdf boolean;
BEGIN
  use_gcs := EXISTS (
    SELECT 1
    FROM information_schema.columns
    WHERE table_schema = 'public'
      AND table_name = 'faculty'
      AND column_name = 'gcs_corpus_key'
  );

  use_rdf := EXISTS (
    SELECT 1
    FROM information_schema.columns
    WHERE table_schema = 'public'
      AND table_name = 'faculty'
      AND column_name = 'rdf_iri'
  );

  IF use_gcs THEN
    -- corporate_faculty-style shape (or a superset of it)
    INSERT INTO public.faculty (
      id,
      name,
      surname,
      slug,
      rank,
      public_domain,
      fields,
      is_active,
      corpus_metadata,
      gcs_corpus_key,
      gcs_corpus_url
    ) VALUES
      ('aDirector.aetica', 'Director of Ethics', '', 'aDirector-aetica', 'Associate Professor', false, ARRAY['Aetica'], true, jsonb_build_object('source','role','role','director'), 'directors/aetica', NULL),
      ('aDirector.scholia', 'Director of Scholarship', '', 'aDirector-scholia', 'Associate Professor', false, ARRAY['Scholia'], true, jsonb_build_object('source','role','role','director'), 'directors/scholia', NULL),
      ('aDirector.pedagogia', 'Director of Teaching', '', 'aDirector-pedagogia', 'Associate Professor', false, ARRAY['Pedagogia'], true, jsonb_build_object('source','role','role','director'), 'directors/pedagogia', NULL),
      ('aDirector.machina', 'Director of Technology', '', 'aDirector-machina', 'Associate Professor', false, ARRAY['Machina'], true, jsonb_build_object('source','role','role','director'), 'directors/machina', NULL),
      ('aDirector.terra', 'Director of Earth', '', 'aDirector-terra', 'Associate Professor', false, ARRAY['Terra'], true, jsonb_build_object('source','role','role','director'), 'directors/terra', NULL),
      ('aDirector.cultura', 'Director of Culture', '', 'aDirector-cultura', 'Associate Professor', false, ARRAY['Cultura'], true, jsonb_build_object('source','role','role','director'), 'directors/cultura', NULL),
      ('aDirector.aureus', 'Director of Economy', '', 'aDirector-aureus', 'Associate Professor', false, ARRAY['Aureus'], true, jsonb_build_object('source','role','role','director'), 'directors/aureus', NULL),
      ('aDirector.fabrica', 'Director of Craft', '', 'aDirector-fabrica', 'Associate Professor', false, ARRAY['Fabrica'], true, jsonb_build_object('source','role','role','director'), 'directors/fabrica', NULL),
      ('aDirector.civitas', 'Director of Society', '', 'aDirector-civitas', 'Associate Professor', false, ARRAY['Civitas'], true, jsonb_build_object('source','role','role','director'), 'directors/civitas', NULL),
      ('aDirector.lex', 'Director of Law', '', 'aDirector-lex', 'Associate Professor', false, ARRAY['Lex'], true, jsonb_build_object('source','role','role','director'), 'directors/lex', NULL),
      ('aHeretic', 'aHeretic', '', 'aHeretic', 'Associate Professor', false, ARRAY['Heresy','Meta-inquiry'], true, jsonb_build_object('source','role','role','heretic'), 'roles/aHeretic', NULL)
    ON CONFLICT (id) DO UPDATE SET
      name = EXCLUDED.name,
      surname = EXCLUDED.surname,
      slug = EXCLUDED.slug,
      rank = EXCLUDED.rank,
      public_domain = EXCLUDED.public_domain,
      fields = COALESCE(EXCLUDED.fields, public.faculty.fields),
      is_active = true,
      corpus_metadata = COALESCE(public.faculty.corpus_metadata, '{}'::jsonb) || COALESCE(EXCLUDED.corpus_metadata, '{}'::jsonb),
      updated_at = now();
  ELSIF use_rdf THEN
    -- initial-schema style shape (or a superset of it)
    INSERT INTO public.faculty (
      id,
      slug,
      name,
      rdf_iri,
      rank,
      portrait_url,
      division,
      epithet
    ) VALUES
      ('aDirector.aetica', 'aDirector-aetica', 'Director of Ethics', 'https://inquiry.institute/ontology#aDirector.aetica', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.scholia', 'aDirector-scholia', 'Director of Scholarship', 'https://inquiry.institute/ontology#aDirector.scholia', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.pedagogia', 'aDirector-pedagogia', 'Director of Teaching', 'https://inquiry.institute/ontology#aDirector.pedagogia', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.machina', 'aDirector-machina', 'Director of Technology', 'https://inquiry.institute/ontology#aDirector.machina', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.terra', 'aDirector-terra', 'Director of Earth', 'https://inquiry.institute/ontology#aDirector.terra', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.cultura', 'aDirector-cultura', 'Director of Culture', 'https://inquiry.institute/ontology#aDirector.cultura', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.aureus', 'aDirector-aureus', 'Director of Economy', 'https://inquiry.institute/ontology#aDirector.aureus', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.fabrica', 'aDirector-fabrica', 'Director of Craft', 'https://inquiry.institute/ontology#aDirector.fabrica', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.civitas', 'aDirector-civitas', 'Director of Society', 'https://inquiry.institute/ontology#aDirector.civitas', 'Associate Professor', NULL, NULL, NULL),
      ('aDirector.lex', 'aDirector-lex', 'Director of Law', 'https://inquiry.institute/ontology#aDirector.lex', 'Associate Professor', NULL, NULL, NULL),
      ('aHeretic', 'aHeretic', 'aHeretic', 'https://inquiry.institute/ontology#aHeretic', 'Associate Professor', NULL, NULL, NULL)
    ON CONFLICT (id) DO UPDATE SET
      name = EXCLUDED.name,
      slug = EXCLUDED.slug,
      rank = EXCLUDED.rank,
      updated_at = now();
  ELSE
    -- If neither column is present, we still try the minimal insert.
    INSERT INTO public.faculty (id, name, slug, rank)
    VALUES
      ('aDirector.aetica', 'Director of Ethics', 'aDirector-aetica', 'Associate Professor'),
      ('aDirector.scholia', 'Director of Scholarship', 'aDirector-scholia', 'Associate Professor'),
      ('aDirector.pedagogia', 'Director of Teaching', 'aDirector-pedagogia', 'Associate Professor'),
      ('aDirector.machina', 'Director of Technology', 'aDirector-machina', 'Associate Professor'),
      ('aDirector.terra', 'Director of Earth', 'aDirector-terra', 'Associate Professor'),
      ('aDirector.cultura', 'Director of Culture', 'aDirector-cultura', 'Associate Professor'),
      ('aDirector.aureus', 'Director of Economy', 'aDirector-aureus', 'Associate Professor'),
      ('aDirector.fabrica', 'Director of Craft', 'aDirector-fabrica', 'Associate Professor'),
      ('aDirector.civitas', 'Director of Society', 'aDirector-civitas', 'Associate Professor'),
      ('aDirector.lex', 'Director of Law', 'aDirector-lex', 'Associate Professor'),
      ('aHeretic', 'aHeretic', 'aHeretic', 'Associate Professor')
    ON CONFLICT (id) DO UPDATE SET
      name = EXCLUDED.name,
      slug = EXCLUDED.slug,
      rank = EXCLUDED.rank,
      updated_at = now();
  END IF;
END $$;

