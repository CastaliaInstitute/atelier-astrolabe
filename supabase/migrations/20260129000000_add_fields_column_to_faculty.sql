-- Add fields column to faculty table if it doesn't exist
-- This stores the academic fields/disciplines for each faculty member

ALTER TABLE public.faculty ADD COLUMN IF NOT EXISTS fields text[];

-- Create GIN index for efficient array queries
CREATE INDEX IF NOT EXISTS idx_faculty_fields ON public.faculty USING GIN(fields) WHERE fields IS NOT NULL;

COMMENT ON COLUMN public.faculty.fields IS 'Array of academic fields/disciplines this faculty member specializes in';
