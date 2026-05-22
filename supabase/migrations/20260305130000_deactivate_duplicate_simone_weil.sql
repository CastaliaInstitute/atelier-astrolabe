-- Deactivate duplicate Simone Weil entries
-- Issue: Three Simone Weil entries, two with incorrect ID formats
-- Keep: a.weil (correct format: a.{surname})
-- Deactivate: a.simone.weil and a.SimoneWeil (incorrect formats with dots/caps)

UPDATE public.faculty
SET is_active = false
WHERE id IN ('a.simone.weil', 'a.SimoneWeil');

-- Verify the fix
DO $$
DECLARE
  v_active_count integer;
  v_correct_entry record;
BEGIN
  -- Count active Simone Weil entries
  SELECT COUNT(*) INTO v_active_count
  FROM public.faculty
  WHERE (name ILIKE '%Simone%' AND surname ILIKE '%Weil%')
    AND is_active = true;
  
  -- Get the correct entry details
  SELECT id, name, surname, rank INTO v_correct_entry
  FROM public.faculty
  WHERE id = 'a.weil';
  
  IF v_active_count = 1 AND v_correct_entry.id = 'a.weil' THEN
    RAISE NOTICE '✅ SUCCESS: Only a.weil is active (name: %, rank: %)', 
      v_correct_entry.name, v_correct_entry.rank;
  ELSE
    RAISE WARNING '⚠️  Expected 1 active Simone Weil (a.weil), found % active entries', v_active_count;
  END IF;
END $$;
