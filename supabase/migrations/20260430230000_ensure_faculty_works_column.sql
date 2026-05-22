-- Ensure `faculty.works` exists (populate-faculty-data / Edge faculty payload).
-- Some environments predating `_applied/013_add_biography_and_works.sql` never got this column.

ALTER TABLE public.faculty
  ADD COLUMN IF NOT EXISTS works jsonb DEFAULT '[]'::jsonb;

CREATE INDEX IF NOT EXISTS idx_faculty_works
  ON public.faculty USING GIN (works)
  WHERE works IS NOT NULL AND jsonb_array_length(works) > 0;

COMMENT ON COLUMN public.faculty.works IS 'JSONB array of works/references. Format: [{"title": "...", "type": "book|article|paper", "url": "...", "source": "..."}]';
