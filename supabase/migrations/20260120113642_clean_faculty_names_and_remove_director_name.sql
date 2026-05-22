-- Clean faculty names: Remove dates and keep only names
-- Remove director_name from board_of_directors (names should come from faculty table)

-- Step 1: Clean up faculty names - remove dates and keep only clean names
UPDATE public.faculty
SET name = CASE id
  WHEN 'a.newton' THEN 'Isaac Newton'
  WHEN 'a.darwin' THEN 'Charles Darwin'
  WHEN 'a.plato' THEN 'Plato'
  WHEN 'a.avicenna' THEN 'Ibn Sina (Avicenna)'
  WHEN 'a.leonardo' THEN 'Leonardo da Vinci'
  WHEN 'a.turing' THEN 'Alan Turing'
  WHEN 'a.alkhwarizmi' THEN 'Al-Khwarizmi'
  WHEN 'a.katsushikaoi' THEN 'Katsushika Ōi'
  WHEN 'a.maryshelley' THEN 'Mary Shelley'
  WHEN 'a.diogenes' THEN 'Diogenes of Sinope'
  WHEN 'a.henryrobert' THEN 'Henry Martyn Robert'
  ELSE name
END
WHERE id IN ('a.newton', 'a.darwin', 'a.plato', 'a.avicenna', 'a.leonardo', 'a.turing', 'a.alkhwarizmi', 'a.katsushikaoi', 'a.maryshelley', 'a.diogenes', 'a.henryrobert');

-- Step 2: Remove director_name column from board_of_directors if it exists
-- (Names should come from faculty table via faculty_id foreign key)
DO $$
BEGIN
  IF EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_schema = 'public' 
    AND table_name = 'board_of_directors' 
    AND column_name = 'director_name'
  ) THEN
    ALTER TABLE public.board_of_directors DROP COLUMN director_name;
    RAISE NOTICE 'Removed director_name column from board_of_directors';
  ELSE
    RAISE NOTICE 'director_name column does not exist in board_of_directors';
  END IF;
END $$;

-- Step 3: Verify names are clean
SELECT id, name FROM public.faculty 
WHERE id IN ('a.newton', 'a.darwin', 'a.plato', 'a.avicenna', 'a.leonardo', 'a.turing', 'a.alkhwarizmi', 'a.katsushikaoi', 'a.maryshelley', 'a.diogenes', 'a.henryrobert')
ORDER BY id;
