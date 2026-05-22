-- Add founding patron field to faculty
-- Used for highlighting founding patron credit on faculty profile pages.

ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS founding_patron text;

COMMENT ON COLUMN public.faculty.founding_patron IS 'Name/credit line for the founding patron associated with this faculty member (if any).';

