-- Add "Fellow" rank to faculty and appoint Diophantus as Fellow
-- 
-- This migration:
-- 1. Updates the faculty_rank_check constraint to include "Fellow"
-- 2. Inserts Diophantus into the faculty table as a Fellow
-- 3. Assigns Diophantus to the College of Mathematics & Logic (math)

-- ============================================================================
-- UPDATE FACULTY RANK CONSTRAINT TO INCLUDE "FELLOW"
-- ============================================================================

-- First, drop the existing constraint
ALTER TABLE public.faculty
DROP CONSTRAINT IF EXISTS faculty_rank_check;

-- Add new constraint with "Fellow" included
ALTER TABLE public.faculty
ADD CONSTRAINT faculty_rank_check 
CHECK (rank IN ('Adjunct', 'Assistant Professor', 'Associate Professor', 'Professor', 'Emeritus', 'Seated', 'Fellow'));

-- ============================================================================
-- INSERT DIOPHANTUS AS FELLOW
-- ============================================================================

-- Insert Diophantus of Alexandria (3rd century CE)
-- Ancient Greek mathematician known for number theory and algebra (Arithmetica)
-- Public domain, so rank is "Fellow" (not "Seated" as that's a different category)
-- Using the schema that matches migration 20260113000003
INSERT INTO public.faculty (
  id, 
  name, 
  surname, 
  biography,
  is_active, 
  rank
) VALUES
('a.diophantus', 'Diophantus', 'Diophantus', 'Ancient Greek mathematician from Alexandria, known as the "father of algebra." His work Arithmetica focused on solving indeterminate equations and number theory problems. His methods influenced later mathematicians including Fermat and Hilbert.', true, 'Fellow')

ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  biography = EXCLUDED.biography,
  is_active = EXCLUDED.is_active,
  rank = EXCLUDED.rank;

-- ============================================================================
-- ASSIGN DIOPHANTUS TO COLLEGE OF MATHEMATICS & LOGIC
-- ============================================================================

INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary) 
VALUES ('a.diophantus', 'math', true)
ON CONFLICT (faculty_id, college_id) DO UPDATE SET
  is_primary = EXCLUDED.is_primary;

-- Comment
COMMENT ON CONSTRAINT faculty_rank_check ON public.faculty IS 
'Faculty rank must be one of: Adjunct, Assistant Professor, Associate Professor, Professor, Emeritus, Seated, or Fellow';
