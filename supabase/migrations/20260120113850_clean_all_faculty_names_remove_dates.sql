-- Clean ALL faculty names: Remove dates, birth/death years, BCE markers, etc.
-- Keep only the person's name

-- Function to clean faculty names by removing date patterns and brackets
CREATE OR REPLACE FUNCTION clean_faculty_name(name_text text)
RETURNS text AS $$
DECLARE
  cleaned text;
BEGIN
  cleaned := name_text;
  
  -- Remove everything after and including date patterns (most comprehensive)
  -- Matches: ", 1877-1970", ", 1866-1930", ", 1643-1727", etc.
  cleaned := regexp_replace(cleaned, ',\s*\d{3,4}[-–]\d{4}.*$', '', 'g');
  
  -- Remove patterns like ", 428? BCE-348? BCE", etc.
  cleaned := regexp_replace(cleaned, ',\s*\d+\??\s*BCE.*$', '', 'g');
  
  -- Remove patterns like ", -1936", ", -1727", etc.
  cleaned := regexp_replace(cleaned, ',\s*-\d{4}.*$', '', 'g');
  
  -- Remove standalone year ranges at end: "1642-1727", "980-1037"
  cleaned := regexp_replace(cleaned, '\s+\d{3,4}[-–]\d{4}.*$', '', 'g');
  
  -- Remove BCE patterns: "428? BCE-348? BCE", "428 BCE"
  cleaned := regexp_replace(cleaned, '\s+\d+\??\s*BCE.*$', '', 'g');
  
  -- Remove brackets and their contents: [Translator], [Illustrator], [Editor], etc.
  cleaned := regexp_replace(cleaned, '\s*\[.*?\]', '', 'g');
  
  -- Remove trailing commas and extra spaces
  cleaned := regexp_replace(cleaned, ',\s*$', '', 'g');
  cleaned := trim(cleaned);
  
  RETURN cleaned;
END;
$$ LANGUAGE plpgsql;

-- Update all faculty names
UPDATE public.faculty
SET name = clean_faculty_name(name)
WHERE name ~ '\d{4}|\d{3,4}\s*BCE|\d{1,2}\?\s*BCE|\d{4}-\d{4}|-\d{4}';

-- Show before/after for verification
SELECT 
  id,
  name as cleaned_name
FROM public.faculty
WHERE name !~ '\d{4}|\d{3,4}\s*BCE|\d{1,2}\?\s*BCE|\d{4}-\d{4}|-\d{4}'
ORDER BY id
LIMIT 20;

-- Drop the function (optional, can keep for future use)
-- DROP FUNCTION IF EXISTS clean_faculty_name(text);
