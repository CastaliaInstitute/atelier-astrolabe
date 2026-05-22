-- Fix director college assignments and set proper ranks
-- Directors should not be adjunct professors and should have valid college slugs

-- First, set all directors to have rank 'Professor' (they are heads of colleges)
UPDATE public.faculty
SET rank = 'Professor'
WHERE id IN (
  SELECT faculty_id 
  FROM public.board_of_directors 
  WHERE faculty_id IS NOT NULL
)
AND (rank IS NULL OR rank = 'Adjunct');

-- Fix college_id assignments for directors
-- Map directors to correct college slugs based on their roles

-- Ada Lovelace -> AINS (Artificial & Inquiring Systems)
UPDATE public.board_of_directors
SET college_id = 'ains'
WHERE director_name = 'Ada Lovelace' AND (college_id IS NULL OR college_id != 'ains');

-- Al-Khwarizmi -> MATH (Mathematics & Logic)
UPDATE public.board_of_directors
SET college_id = 'math'
WHERE director_name = 'Al-Khwarizmi' AND (college_id IS NULL OR college_id != 'math');

-- Confucius -> META (Metaphysics & Mysticism) - philosophy/ethics
UPDATE public.board_of_directors
SET college_id = 'meta'
WHERE director_name = 'Confucius' AND (college_id IS NULL OR college_id LIKE '%-%-%-%-%');

-- Diogenes of Sinope -> Keep NULL (Heretic, not a college director)
-- No change needed

-- Ibn al-Haytham -> META (Metaphysics & Mysticism) - optics/philosophy
UPDATE public.board_of_directors
SET college_id = 'meta'
WHERE director_name = 'Ibn al-Haytham' AND (college_id IS NULL OR college_id != 'meta');

-- Ibn Sina (Avicenna) -> HEAL (Health, Embodiment & Medicine)
UPDATE public.board_of_directors
SET college_id = 'heal'
WHERE director_name = 'Ibn Sina (Avicenna)' AND (college_id IS NULL OR college_id != 'heal');

-- Katsushika Ōi -> ARTS (Arts & Imagination)
UPDATE public.board_of_directors
SET college_id = 'arts'
WHERE director_name = 'Katsushika Ōi' AND (college_id IS NULL OR college_id != 'arts');

-- Leonardo da Vinci -> CRAF (Craft, Engineering & Fabrication)
UPDATE public.board_of_directors
SET college_id = 'craf'
WHERE director_name = 'Leonardo da Vinci' AND (college_id IS NULL OR college_id LIKE '%-%-%-%-%');

-- Maria Sibylla Merian -> ELAG (Earth, Life & Agriculture) - naturalist/illustrator
UPDATE public.board_of_directors
SET college_id = 'elag'
WHERE director_name = 'Maria Sibylla Merian' AND (college_id IS NULL OR college_id LIKE '%-%-%-%-%');

-- Mary Shelley -> HUM (Humanities & Letters) - literature
UPDATE public.board_of_directors
SET college_id = 'humn'
WHERE director_name = 'Mary Shelley' AND (college_id IS NULL OR college_id LIKE '%-%-%-%-%');

-- Zhuangzi -> META (Metaphysics & Mysticism) - philosophy
UPDATE public.board_of_directors
SET college_id = 'meta'
WHERE director_name = 'Zhuangzi' AND (college_id IS NULL OR college_id LIKE '%-%-%-%-%');

-- Ensure no directors are marked as Adjunct
UPDATE public.faculty
SET rank = 'Professor'
WHERE id IN (
  SELECT faculty_id 
  FROM public.board_of_directors 
  WHERE faculty_id IS NOT NULL
)
AND rank = 'Adjunct';
