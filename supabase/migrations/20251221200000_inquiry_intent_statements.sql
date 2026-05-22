-- Inquiry Institute intent statements
-- Stores a user's "Why I want to be a part of Inquiry Institute" statement.

CREATE TABLE IF NOT EXISTS public.inquiry_intent_statements (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id uuid NOT NULL REFERENCES public.profiles(id) ON DELETE CASCADE,
  -- Optional provenance: which faculty page the user was on when drafting/saving
  source_faculty_id text REFERENCES public.faculty(id) ON DELETE SET NULL,
  statement text NOT NULL DEFAULT '',
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  UNIQUE(user_id)
);

CREATE INDEX IF NOT EXISTS idx_inquiry_intent_statements_user_id
  ON public.inquiry_intent_statements(user_id);

CREATE INDEX IF NOT EXISTS idx_inquiry_intent_statements_source_faculty_id
  ON public.inquiry_intent_statements(source_faculty_id);

-- updated_at trigger (function defined in 001_initial_schema.sql)
DROP TRIGGER IF EXISTS update_inquiry_intent_statements_updated_at ON public.inquiry_intent_statements;
CREATE TRIGGER update_inquiry_intent_statements_updated_at
  BEFORE UPDATE ON public.inquiry_intent_statements
  FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- RLS
ALTER TABLE public.inquiry_intent_statements ENABLE ROW LEVEL SECURITY;

-- Users can read their own statement
DROP POLICY IF EXISTS "Users can view own inquiry intent statement" ON public.inquiry_intent_statements;
CREATE POLICY "Users can view own inquiry intent statement"
  ON public.inquiry_intent_statements
  FOR SELECT
  USING (auth.uid() = user_id);

-- Users can insert their own statement
DROP POLICY IF EXISTS "Users can create own inquiry intent statement" ON public.inquiry_intent_statements;
CREATE POLICY "Users can create own inquiry intent statement"
  ON public.inquiry_intent_statements
  FOR INSERT
  WITH CHECK (auth.uid() = user_id);

-- Users can update their own statement
DROP POLICY IF EXISTS "Users can update own inquiry intent statement" ON public.inquiry_intent_statements;
CREATE POLICY "Users can update own inquiry intent statement"
  ON public.inquiry_intent_statements
  FOR UPDATE
  USING (auth.uid() = user_id)
  WITH CHECK (auth.uid() = user_id);

COMMENT ON TABLE public.inquiry_intent_statements IS 'Per-user "Why I want to be part of Inquiry Institute" statements.';
