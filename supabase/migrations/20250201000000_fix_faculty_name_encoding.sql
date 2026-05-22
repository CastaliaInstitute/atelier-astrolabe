-- Fix UTF-8 encoding issues in faculty names
-- Common issues: double-encoded UTF-8, incorrect character mappings
-- This migration fixes encoding problems in the name and surname columns

-- Function to fix common UTF-8 encoding issues
-- This handles cases where UTF-8 was interpreted as Latin-1 and re-encoded
CREATE OR REPLACE FUNCTION fix_utf8_encoding(text_value text)
RETURNS text
LANGUAGE plpgsql
AS $$
DECLARE
  fixed text;
BEGIN
  fixed := text_value;
  
  -- Most common issue: UTF-8 bytes interpreted as Latin-1, then re-encoded as UTF-8
  -- Pattern: Original UTF-8 byte sequence -> Latin-1 interpretation -> UTF-8 re-encoding
  
  -- Fix common double-encoded UTF-8 issues (most frequent patterns first)
  -- These are the result of UTF-8 -> Latin-1 -> UTF-8 double encoding
  
  -- Latin accented characters (most common)
  fixed := REPLACE(fixed, 'Ã€', 'À');  -- C380 -> À
  fixed := REPLACE(fixed, 'Ã¡', 'á');  -- C3A1 -> á
  fixed := REPLACE(fixed, 'Ã©', 'é');  -- C3A9 -> é
  fixed := REPLACE(fixed, 'Ã­', 'í');  -- C3AD -> í
  fixed := REPLACE(fixed, 'Ã³', 'ó');  -- C3B3 -> ó
  fixed := REPLACE(fixed, 'Ãº', 'ú');  -- C3BA -> ú
  fixed := REPLACE(fixed, 'Ã½', 'ý');  -- C3BD -> ý
  fixed := REPLACE(fixed, 'Ã§', 'ç');  -- C3A7 -> ç
  fixed := REPLACE(fixed, 'Ã±', 'ñ');  -- C3B1 -> ñ
  
  -- Uppercase variants
  fixed := REPLACE(fixed, 'Ã', 'Á');   -- C381 -> Á
  fixed := REPLACE(fixed, 'Ã‰', 'É');  -- C389 -> É
  fixed := REPLACE(fixed, 'Ã', 'Í');   -- C38D -> Í
  fixed := REPLACE(fixed, 'Ã"', 'Ó');  -- C393 -> Ó
  fixed := REPLACE(fixed, 'Ãš', 'Ú');  -- C39A -> Ú
  fixed := REPLACE(fixed, 'Ã', 'Ý');   -- C39D -> Ý
  fixed := REPLACE(fixed, 'Ã‡', 'Ç');  -- C387 -> Ç
  fixed := REPLACE(fixed, 'Ã', 'Ñ');   -- C391 -> Ñ
  
  -- German umlauts
  fixed := REPLACE(fixed, 'Ã¤', 'ä');  -- C3A4 -> ä
  fixed := REPLACE(fixed, 'Ã«', 'ë');  -- C3AB -> ë
  fixed := REPLACE(fixed, 'Ã¯', 'ï');  -- C3AF -> ï
  fixed := REPLACE(fixed, 'Ã¶', 'ö');  -- C3B6 -> ö
  fixed := REPLACE(fixed, 'Ã¼', 'ü');  -- C3BC -> ü
  fixed := REPLACE(fixed, 'ÃŸ', 'ß');  -- C39F -> ß
  
  -- Uppercase umlauts
  fixed := REPLACE(fixed, 'Ã„', 'Ä');  -- C384 -> Ä
  fixed := REPLACE(fixed, 'Ã‹', 'Ë');  -- C38B -> Ë
  fixed := REPLACE(fixed, 'Ã', 'Ï');   -- C38F -> Ï
  fixed := REPLACE(fixed, 'Ã–', 'Ö');  -- C396 -> Ö
  fixed := REPLACE(fixed, 'Ãœ', 'Ü');  -- C39C -> Ü
  
  -- Scandinavian characters
  fixed := REPLACE(fixed, 'Ã¥', 'å');  -- C3A5 -> å
  fixed := REPLACE(fixed, 'Ã…', 'Å');  -- C385 -> Å
  fixed := REPLACE(fixed, 'Ã¦', 'æ');  -- C3A6 -> æ
  fixed := REPLACE(fixed, 'Ã†', 'Æ');  -- C386 -> Æ
  fixed := REPLACE(fixed, 'Ã¸', 'ø');  -- C3B8 -> ø
  fixed := REPLACE(fixed, 'Ã˜', 'Ø');  -- C398 -> Ø
  
  -- Polish characters (common in names)
  fixed := REPLACE(fixed, 'Å', 'Ł');   -- C581 -> Ł
  fixed := REPLACE(fixed, 'Åƒ', 'ł');  -- C582 -> ł
  fixed := REPLACE(fixed, 'Å„', 'ń');  -- C584 -> ń
  fixed := REPLACE(fixed, 'Åš', 'ś');  -- C59B -> ś
  fixed := REPLACE(fixed, 'Å¹', 'ź');  -- C5BA -> ź
  fixed := REPLACE(fixed, 'Å¼', 'ż');  -- C5BC -> ż
  
  -- Czech/Slovak characters
  fixed := REPLACE(fixed, 'Å', 'č');   -- C48D -> č
  fixed := REPLACE(fixed, 'Å', 'ď');   -- C48F -> ď
  fixed := REPLACE(fixed, 'Ä›', 'ě');  -- C4B9 -> ě
  fixed := REPLACE(fixed, 'Å', 'ň');   -- C588 -> ň
  fixed := REPLACE(fixed, 'Å™', 'ř');  -- C599 -> ř
  fixed := REPLACE(fixed, 'Å¡', 'š');  -- C5A1 -> š
  fixed := REPLACE(fixed, 'Å¥', 'ť');  -- C5A5 -> ť
  fixed := REPLACE(fixed, 'Å¯', 'ů');  -- C5AF -> ů
  fixed := REPLACE(fixed, 'Å¾', 'ž');  -- C5BE -> ž
  
  -- Romanian characters
  fixed := REPLACE(fixed, 'Ã¢', 'â');  -- C382 -> â
  fixed := REPLACE(fixed, 'Ã®', 'î');  -- C3AE -> î
  fixed := REPLACE(fixed, 'Ã', 'ă');   -- C483 -> ă
  fixed := REPLACE(fixed, 'Åž', 'ș');  -- C59E -> ș
  fixed := REPLACE(fixed, 'Å¢', 'ț');  -- C5A2 -> ț
  
  RETURN TRIM(fixed);
END;
$$;

-- Update faculty names that have encoding issues
-- Only update records that actually contain problematic patterns
UPDATE public.faculty
SET 
  name = fix_utf8_encoding(name),
  surname = fix_utf8_encoding(surname),
  updated_at = now()
WHERE 
  name ~ '[ÃÅ]' OR surname ~ '[ÃÅ]';

-- Report on what was fixed
DO $$
DECLARE
  before_count integer;
  after_count integer;
BEGIN
  -- Count records with encoding issues before fix
  SELECT COUNT(*) INTO before_count
  FROM public.faculty
  WHERE name ~ '[ÃÅ]' OR surname ~ '[ÃÅ]';
  
  -- After the UPDATE above, count remaining issues
  SELECT COUNT(*) INTO after_count
  FROM public.faculty
  WHERE name ~ '[ÃÅ]' OR surname ~ '[ÃÅ]';
  
  RAISE NOTICE 'Fixed encoding issues: % records had issues, % remain after fix', 
    before_count, after_count;
END;
$$;

-- Drop the function after use (optional - can keep for future use)
-- DROP FUNCTION IF EXISTS fix_utf8_encoding(text);

COMMENT ON FUNCTION fix_utf8_encoding(text) IS 'Fixes common UTF-8 double-encoding issues in text fields';
