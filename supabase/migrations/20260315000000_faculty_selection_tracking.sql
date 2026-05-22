-- Faculty Selection Tracking for Cascading Faculty Population
-- Tracks who selected whom during the director→10→100→1000 cascade.

-- Selection phase: 1 = director picks 10, 2 = 11 pick 100, 3 = 111 pick ~890
ALTER TABLE public.faculty ADD COLUMN IF NOT EXISTS selection_phase integer;
ALTER TABLE public.faculty ADD COLUMN IF NOT EXISTS selected_by text REFERENCES public.faculty(id);
ALTER TABLE public.faculty ADD COLUMN IF NOT EXISTS generation_batch text;

CREATE INDEX IF NOT EXISTS idx_faculty_selection_phase ON public.faculty(selection_phase) WHERE selection_phase IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_faculty_selected_by ON public.faculty(selected_by) WHERE selected_by IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_faculty_generation_batch ON public.faculty(generation_batch) WHERE generation_batch IS NOT NULL;

COMMENT ON COLUMN public.faculty.selection_phase IS 'Cascading selection phase: 1=director pick, 2=first-circle pick, 3=second-circle pick';
COMMENT ON COLUMN public.faculty.selected_by IS 'Faculty ID of the selector who chose this faculty member';
COMMENT ON COLUMN public.faculty.generation_batch IS 'Batch identifier for the Cloud Run job that created this faculty member';
