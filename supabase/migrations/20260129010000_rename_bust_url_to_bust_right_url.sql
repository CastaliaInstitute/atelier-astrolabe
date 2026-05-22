-- Rename bust_url to bust_right_url for clarity
-- The angled busts face RIGHT and can be CSS-mirrored for left placement
-- bust_frontal_url remains unchanged (symmetrical front view)

-- Rename the column (only if old name exists)
DO $$
BEGIN
  IF EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_schema = 'public' 
    AND table_name = 'faculty' 
    AND column_name = 'bust_url'
  ) THEN
    ALTER TABLE public.faculty RENAME COLUMN bust_url TO bust_right_url;
  END IF;
END $$;

-- Update the comment (only if column exists)
DO $$
BEGIN
  IF EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_schema = 'public' 
    AND table_name = 'faculty' 
    AND column_name = 'bust_right_url'
  ) THEN
    COMMENT ON COLUMN public.faculty.bust_right_url IS 'URL to right-facing (3/4 view) marble bust. Can be CSS-mirrored (scaleX: -1) for left-facing placement.';
  END IF;
END $$;

-- Update the index name for consistency
DROP INDEX IF EXISTS idx_faculty_bust_url;
CREATE INDEX IF NOT EXISTS idx_faculty_bust_right_url
ON public.faculty(bust_right_url)
WHERE bust_right_url IS NOT NULL;
