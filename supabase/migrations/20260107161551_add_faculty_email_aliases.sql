-- Add email_alias field to faculty table
-- Faculty should have emails like a.plato@inquiry.institute

-- Add email_alias column to faculty table
ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS email_alias text;

-- Create index for email lookups
CREATE INDEX IF NOT EXISTS idx_faculty_email_alias 
  ON public.faculty(email_alias) 
  WHERE email_alias IS NOT NULL;

-- Create unique constraint on email_alias
CREATE UNIQUE INDEX IF NOT EXISTS idx_faculty_email_alias_unique 
  ON public.faculty(email_alias) 
  WHERE email_alias IS NOT NULL;

-- Function to generate email alias from slug
-- Converts slug (e.g., 'a.plato') to email (e.g., 'a.plato@inquiry.institute')
CREATE OR REPLACE FUNCTION public.generate_faculty_email_alias(faculty_slug text)
RETURNS text AS $$
BEGIN
  -- Normalize slug and append domain
  RETURN LOWER(faculty_slug) || '@inquiry.institute';
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- Populate email aliases for existing faculty based on their id (slug)
-- Faculty IDs follow pattern like 'a.plato', 'a.aristotle', etc.
UPDATE public.faculty
SET email_alias = public.generate_faculty_email_alias(id)
WHERE email_alias IS NULL 
  AND id IS NOT NULL
  AND id != '';

-- Add trigger to auto-generate email_alias on insert/update if not provided
CREATE OR REPLACE FUNCTION public.set_faculty_email_alias()
RETURNS TRIGGER AS $$
BEGIN
  -- If email_alias is not set and id is provided, generate it
  IF NEW.email_alias IS NULL AND NEW.id IS NOT NULL AND NEW.id != '' THEN
    NEW.email_alias := public.generate_faculty_email_alias(NEW.id);
  END IF;
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Create trigger
DROP TRIGGER IF EXISTS trigger_set_faculty_email_alias ON public.faculty;
CREATE TRIGGER trigger_set_faculty_email_alias
  BEFORE INSERT OR UPDATE ON public.faculty
  FOR EACH ROW
  EXECUTE FUNCTION public.set_faculty_email_alias();

-- Add comment
COMMENT ON COLUMN public.faculty.email_alias IS 'Email alias for faculty member (e.g., a.plato@inquiry.institute). Auto-generated from id if not provided.';

