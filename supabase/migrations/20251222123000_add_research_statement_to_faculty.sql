-- Add research_statement column to faculty table
-- research_statement: short statement derived from stored Wikipedia text

ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS research_statement text;

-- Full-text search index on research_statement
CREATE INDEX IF NOT EXISTS idx_faculty_research_statement_fts
ON public.faculty
USING gin(to_tsvector('english', coalesce(research_statement, '')));

COMMENT ON COLUMN public.faculty.research_statement IS 'AI-generated research statement describing what research the faculty member would pursue if funded at the Inquiry Institute. Framed as "If funded, I will..." statement derived from stored Wikipedia text; suitable for display on faculty pages';

