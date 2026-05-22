-- Add frontal bust URL column to faculty table
-- bust_url = angled/3-quarter view (can mirror for left/right placement)
-- bust_frontal_url = straight-on frontal view

-- Rename existing column for clarity (optional - keep backwards compatible)
COMMENT ON COLUMN public.faculty.bust_url IS 'URL to angled (3/4 view) marble bust. Can be mirrored for left/right table placement.';

-- Add frontal bust column
ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS bust_frontal_url text;

COMMENT ON COLUMN public.faculty.bust_frontal_url IS 'URL to frontal (straight-on) marble bust for symmetrical displays.';

-- Create index for frontal bust lookups
CREATE INDEX IF NOT EXISTS idx_faculty_bust_frontal_url
ON public.faculty(bust_frontal_url)
WHERE bust_frontal_url IS NOT NULL;
