-- Assign Board of Directors to their colleges in faculty_colleges
-- So directors appear under "Faculty by College" and college counts are correct.
-- Source of truth: board_of_directors (one director per college).

-- Insert or update faculty_colleges for each college director
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
SELECT bd.faculty_id, bd.college_id, true
FROM public.board_of_directors bd
WHERE bd.position_type = 'college'
  AND bd.college_id IS NOT NULL
  AND bd.faculty_id IS NOT NULL
ON CONFLICT (faculty_id, college_id) DO UPDATE SET is_primary = true;

-- Log result
DO $$
DECLARE
  v_count integer;
BEGIN
  SELECT COUNT(*) INTO v_count
  FROM public.board_of_directors
  WHERE position_type = 'college' AND college_id IS NOT NULL AND faculty_id IS NOT NULL;
  RAISE NOTICE 'Assigned % directors to colleges in faculty_colleges', v_count;
END $$;
