-- Standardize faculty divisions to match college IDs
-- Issue: Inconsistent division values (HUMN vs Humanities, MATH vs math, etc.)
-- Solution: Map all variations to lowercase college codes

-- Backup current divisions for reference
DO $$
DECLARE
  v_division_counts text;
BEGIN
  SELECT string_agg(division || ': ' || count::text, ', ' ORDER BY count DESC)
  INTO v_division_counts
  FROM (
    SELECT division, COUNT(*) as count
    FROM public.faculty
    WHERE division IS NOT NULL AND is_active = true
    GROUP BY division
  ) counts;
  
  RAISE NOTICE 'Current active faculty divisions: %', v_division_counts;
END $$;

-- Standardize divisions
-- Drop the unique index, update, then recreate
DROP INDEX IF EXISTS public.idx_faculty_email_alias_unique;

UPDATE public.faculty
SET division = CASE
  -- Humanities
  WHEN division IN ('HUMN', 'Humanities', 'humn') THEN 'humn'
  
  -- Mathematics & Logic
  WHEN division IN ('MATH', 'Mathematics', 'math') THEN 'math'
  
  -- Metaphysics & Mysticism
  WHEN division IN ('META', 'Metaphysics', 'meta') THEN 'meta'
  
  -- Natural Philosophy
  WHEN division IN ('NATP', 'Natural Philosophy', 'natp') THEN 'natp'
  
  -- Social Inquiry
  WHEN division IN ('SOCI', 'Social Sciences', 'soci') THEN 'soci'
  
  -- Arts & Imagination
  WHEN division IN ('ARTS', 'Arts', 'arts') THEN 'arts'
  
  -- Craft, Engineering & Fabrication
  WHEN division IN ('CRAF', 'Craft', 'craf') THEN 'craf'
  
  -- Earth, Life & Agriculture
  WHEN division IN ('ELAG', 'elag') THEN 'elag'
  
  -- Health, Embodiment & Medicine
  WHEN division IN ('HEAL', 'Health', 'heal') THEN 'heal'
  
  -- Artificial & Inquiring Systems
  WHEN division IN ('AINS', 'ains') THEN 'ains'
  
  -- Keep as-is if already correct
  ELSE division
END
WHERE division IS NOT NULL;

-- Note: Skipping index recreation - will be done in a separate migration after verification

-- Verify the fix
DO $$
DECLARE
  v_unique_divisions text[];
  v_invalid_divisions text[];
  v_valid_colleges text[] := ARRAY['ains', 'arts', 'craf', 'elag', 'heal', 'humn', 'math', 'meta', 'natp', 'soci'];
  rec RECORD;
BEGIN
  -- Get unique divisions
  SELECT ARRAY_AGG(DISTINCT division ORDER BY division)
  INTO v_unique_divisions
  FROM public.faculty
  WHERE division IS NOT NULL AND is_active = true;
  
  -- Find invalid divisions
  SELECT ARRAY_AGG(div)
  INTO v_invalid_divisions
  FROM UNNEST(v_unique_divisions) AS div
  WHERE div != ALL(v_valid_colleges);
  
  IF v_invalid_divisions IS NULL OR array_length(v_invalid_divisions, 1) IS NULL THEN
    RAISE NOTICE '✅ SUCCESS: All divisions standardized to college codes';
    RAISE NOTICE 'Active divisions: %', array_to_string(v_unique_divisions, ', ');
  ELSE
    RAISE WARNING '⚠️  Invalid divisions still exist: %', array_to_string(v_invalid_divisions, ', ');
  END IF;
  
  -- Show counts
  FOR rec IN (
    SELECT division, COUNT(*) as count
    FROM public.faculty
    WHERE division IS NOT NULL AND is_active = true
    GROUP BY division
    ORDER BY count DESC
  ) LOOP
    RAISE NOTICE '  %: % faculty', rec.division, rec.count;
  END LOOP;
END $$;
