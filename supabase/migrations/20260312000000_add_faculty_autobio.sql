-- Add autobio column to faculty: first-person autobiographical statement for the About section.
-- Display as "I am a.einstein, ..." etc.

ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS autobio text;

COMMENT ON COLUMN public.faculty.autobio IS 'First-person autobiographical statement for the About section (e.g. "I am a.einstein, ..."). Used on faculty profile and college faculty pages.';

-- Optional: backfill from biography where it looks first-person (starts with "I am ")
UPDATE public.faculty
SET autobio = biography
WHERE autobio IS NULL
  AND biography IS NOT NULL
  AND trim(biography) ~* '^\s*I\s+am\s+';
