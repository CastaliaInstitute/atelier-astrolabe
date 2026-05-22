-- Faculty research statements
-- Stores a single, public-facing research statement per faculty slug.

CREATE TABLE IF NOT EXISTS public.faculty_join_statements (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  faculty_slug text NOT NULL UNIQUE,
  faculty_id text REFERENCES public.faculty(id) ON DELETE SET NULL,
  statement text NOT NULL DEFAULT '',
  generated_at timestamptz,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_faculty_join_statements_faculty_slug
  ON public.faculty_join_statements(faculty_slug);

CREATE INDEX IF NOT EXISTS idx_faculty_join_statements_faculty_id
  ON public.faculty_join_statements(faculty_id);

-- updated_at trigger (function defined in 001_initial_schema.sql)
DROP TRIGGER IF EXISTS update_faculty_join_statements_updated_at ON public.faculty_join_statements;
CREATE TRIGGER update_faculty_join_statements_updated_at
  BEFORE UPDATE ON public.faculty_join_statements
  FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- RLS
ALTER TABLE public.faculty_join_statements ENABLE ROW LEVEL SECURITY;

-- Public can read faculty statements
DROP POLICY IF EXISTS "Public can view faculty join statements" ON public.faculty_join_statements;
CREATE POLICY "Public can view faculty join statements"
  ON public.faculty_join_statements
  FOR SELECT
  USING (true);

-- Only service role can insert/update/delete
DROP POLICY IF EXISTS "Service role can manage faculty join statements" ON public.faculty_join_statements;
CREATE POLICY "Service role can manage faculty join statements"
  ON public.faculty_join_statements
  FOR ALL
  TO service_role
  USING (true)
  WITH CHECK (true);

COMMENT ON TABLE public.faculty_join_statements IS 'Per-faculty public research statement (auto-generated if missing).';
