-- Commonplace Digital Scholarly Library Schema
-- Creates persons and works tables for the Commonplace system
-- This allows Directus to manage Commonplace content

-- ============================================================================
-- PERSONS TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.persons (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name text NOT NULL,
  slug text UNIQUE NOT NULL,
  kind text NOT NULL CHECK (kind IN ('faculty', 'external_author', 'student', 'other')),
  public_domain boolean DEFAULT false,
  bio text,
  portrait_url text,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_persons_slug ON public.persons(slug);
CREATE INDEX IF NOT EXISTS idx_persons_kind ON public.persons(kind);
CREATE INDEX IF NOT EXISTS idx_persons_public_domain ON public.persons(public_domain);

-- ============================================================================
-- WORKS TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.works (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  title text NOT NULL,
  slug text UNIQUE NOT NULL,
  primary_author_id uuid REFERENCES public.persons(id) ON DELETE SET NULL,
  abstract text,
  content_md text,
  work_type text CHECK (work_type IN ('essay', 'note', 'lecture', 'fragment_collection', 'review_article', 'other')),
  status text NOT NULL CHECK (status IN ('draft', 'submitted', 'under_review', 'published', 'archived')) DEFAULT 'draft',
  visibility text NOT NULL CHECK (visibility IN ('public', 'private', 'unlisted')) DEFAULT 'private',
  cover_image text,
  flipbook_mode text,
  flipbook_manifest jsonb,
  publication_date date,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_works_slug ON public.works(slug);
CREATE INDEX IF NOT EXISTS idx_works_primary_author_id ON public.works(primary_author_id);
CREATE INDEX IF NOT EXISTS idx_works_status ON public.works(status);
CREATE INDEX IF NOT EXISTS idx_works_visibility ON public.works(visibility);
CREATE INDEX IF NOT EXISTS idx_works_status_visibility ON public.works(status, visibility) WHERE status = 'published' AND visibility = 'public';

-- ============================================================================
-- ROW LEVEL SECURITY
-- ============================================================================

ALTER TABLE public.persons ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.works ENABLE ROW LEVEL SECURITY;

-- Public can read public_domain persons
CREATE POLICY "Public can read public domain persons"
  ON public.persons FOR SELECT
  TO anon, authenticated
  USING (public_domain = true);

-- Public can read published, public works
CREATE POLICY "Public can read published public works"
  ON public.works FOR SELECT
  TO anon, authenticated
  USING (status = 'published' AND visibility = 'public');

-- Service role has full access
CREATE POLICY "Service role full access to persons"
  ON public.persons FOR ALL
  TO service_role
  USING (true)
  WITH CHECK (true);

CREATE POLICY "Service role full access to works"
  ON public.works FOR ALL
  TO service_role
  USING (true)
  WITH CHECK (true);

-- ============================================================================
-- TRIGGERS
-- ============================================================================

CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_persons_updated_at
  BEFORE UPDATE ON public.persons
  FOR EACH ROW
  EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_works_updated_at
  BEFORE UPDATE ON public.works
  FOR EACH ROW
  EXECUTE FUNCTION update_updated_at_column();
