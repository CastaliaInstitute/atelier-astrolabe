-- College of Social Inquiry: replace director with a better thematic fit.
-- Henry Martyn Robert (a.henryrobert) is the Parliamentarian (procedure, Robert's Rules);
-- he remains in room_faculty_membership as parliamentarian but is a poor fit for
-- "Social Inquiry" (sociology, political economy, governance, society).
-- Use a PD figure: Adam Smith (a.adamsmith) from Gutenberg — political economy,
-- moral philosophy, society; matches the other college directors (all PD).

UPDATE public.board_of_directors
SET faculty_id = 'a.adamsmith'
WHERE college_id = 'soci'
  AND position_type = 'college'
  AND faculty_id = 'a.henryrobert';

-- Ensure Adam Smith is assigned to SOC in faculty_colleges if not already
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
SELECT 'a.adamsmith', 'soci', true
WHERE EXISTS (SELECT 1 FROM public.faculty WHERE id = 'a.adamsmith')
  AND NOT EXISTS (SELECT 1 FROM public.faculty_colleges WHERE faculty_id = 'a.adamsmith' AND college_id = 'soci')
ON CONFLICT (faculty_id, college_id) DO UPDATE SET is_primary = true;
