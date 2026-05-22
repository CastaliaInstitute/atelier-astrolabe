-- Populate Woka (WorkAdventure avatar) designs for Board of Directors
-- Each design reflects the historical period, culture, and field of expertise

-- 1. Alan Turing (a.turing) - AINS - Computer Science Pioneer
UPDATE public.faculty
SET woka = '{
  "texture_id": "turing_collection",
  "body": "body_scientist_modern",
  "hair": "hair_short_neat",
  "eyes": "eyes_intelligent",
  "accessories": ["tie_formal", "glasses_round"],
  "colors": {
    "skin": "#f4d4a3",
    "hair": "#8b6f47",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "20th century British mathematician and computer scientist - formal academic attire"
  }
}'::jsonb
WHERE id = 'a.turing';

-- 2. Katsushika Ōi (a.katsushikaoi) - ARTS - Japanese Artist
UPDATE public.faculty
SET woka = '{
  "texture_id": "oi_collection",
  "body": "body_artist_japanese",
  "hair": "hair_japanese_traditional",
  "eyes": "eyes_artistic",
  "accessories": ["kimono_artist", "brush_set"],
  "colors": {
    "skin": "#f4d4a3",
    "hair": "#2c1810",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "19th century Japanese ukiyo-e artist - traditional Japanese attire with artistic accessories"
  }
}'::jsonb
WHERE id = 'a.katsushikaoi';

-- 3. Leonardo da Vinci (a.davinci) - CRAF - Renaissance Polymath
UPDATE public.faculty
SET woka = '{
  "texture_id": "davinci_collection",
  "body": "body_renaissance_scholar",
  "hair": "hair_renaissance_long",
  "eyes": "eyes_curious",
  "accessories": ["robe_renaissance", "sketchbook", "compass"],
  "colors": {
    "skin": "#e8c4a0",
    "hair": "#8b6f47",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "15th-16th century Italian Renaissance master - flowing robes with artistic and scientific tools"
  }
}'::jsonb
WHERE id = 'a.davinci';

-- 4. Charles Darwin (a.darwin) - ELAG - Naturalist
UPDATE public.faculty
SET woka = '{
  "texture_id": "darwin_collection",
  "body": "body_victorian_scientist",
  "hair": "hair_victorian_beard",
  "eyes": "eyes_observant",
  "accessories": ["coat_victorian", "magnifying_glass", "notebook"],
  "colors": {
    "skin": "#f4d4a3",
    "hair": "#8b6f47",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "19th century British naturalist - Victorian era field scientist with exploration gear"
  }
}'::jsonb
WHERE id = 'a.darwin';

-- 5. Ibn Sina / Avicenna (a.avicenna) - HEAL - Persian Physician
UPDATE public.faculty
SET woka = '{
  "texture_id": "avicenna_collection",
  "body": "body_medieval_scholar",
  "hair": "hair_middle_eastern",
  "eyes": "eyes_wise",
  "accessories": ["robe_islamic", "medical_scroll", "herbs"],
  "colors": {
    "skin": "#d4a574",
    "hair": "#2c1810",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "10th-11th century Persian physician and philosopher - Islamic Golden Age scholar with medical tools"
  }
}'::jsonb
WHERE id = 'a.avicenna';

-- 6. Mary Shelley (a.maryshelley) - HUMN - Gothic Novelist
UPDATE public.faculty
SET woka = '{
  "texture_id": "shelley_collection",
  "body": "body_romantic_era",
  "hair": "hair_romantic_curls",
  "eyes": "eyes_imaginative",
  "accessories": ["dress_romantic", "quill_pen", "manuscript"],
  "colors": {
    "skin": "#f4d4a3",
    "hair": "#2c1810",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "19th century British author - Romantic era writer with literary accessories"
  }
}'::jsonb
WHERE id = 'a.maryshelley';

-- 7. Al-Khwarizmi (a.alkhwarizmi) - MATH - Persian Mathematician
UPDATE public.faculty
SET woka = '{
  "texture_id": "khwarizmi_collection",
  "body": "body_abbasid_scholar",
  "hair": "hair_middle_eastern",
  "eyes": "eyes_analytical",
  "accessories": ["robe_abbasid", "abacus", "mathematical_scroll"],
  "colors": {
    "skin": "#d4a574",
    "hair": "#2c1810",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "9th century Persian mathematician - Abbasid Caliphate scholar with mathematical instruments"
  }
}'::jsonb
WHERE id = 'a.alkhwarizmi';

-- 8. Plato (a.plato) - META - Ancient Greek Philosopher
UPDATE public.faculty
SET woka = '{
  "texture_id": "plato_collection",
  "body": "body_ancient_greek",
  "hair": "hair_ancient_greek_beard",
  "eyes": "eyes_philosophical",
  "accessories": ["chiton_greek", "scroll_philosophy", "laurel_wreath"],
  "colors": {
    "skin": "#e8c4a0",
    "hair": "#8b6f47",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "4th century BCE Greek philosopher - classical Athenian scholar with philosophical scrolls"
  }
}'::jsonb
WHERE id = 'a.plato';

-- 9. Isaac Newton (a.newton) - NATP - Physicist & Mathematician
UPDATE public.faculty
SET woka = '{
  "texture_id": "newton_collection",
  "body": "body_17th_century_scholar",
  "hair": "hair_17th_century_wig",
  "eyes": "eyes_brilliant",
  "accessories": ["coat_17th_century", "prism", "telescope"],
  "colors": {
    "skin": "#f4d4a3",
    "hair": "#ffffff",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "17th-18th century English physicist - Scientific Revolution era with optical instruments"
  }
}'::jsonb
WHERE id = 'a.newton';

-- 10. Henry Martyn Robert (a.henryrobert) - SOCI - Parliamentarian
UPDATE public.faculty
SET woka = '{
  "texture_id": "robert_collection",
  "body": "body_19th_century_formal",
  "hair": "hair_19th_century_neat",
  "eyes": "eyes_authoritative",
  "accessories": ["suit_formal", "roberts_rules_book", "gavel"],
  "colors": {
    "skin": "#f4d4a3",
    "hair": "#8b6f47",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "19th-20th century American parliamentarian - formal business attire with parliamentary tools"
  }
}'::jsonb
WHERE id = 'a.henryrobert';

-- 11. Diogenes of Sinope (a.diogenes) - Heretic - Cynic Philosopher
UPDATE public.faculty
SET woka = '{
  "texture_id": "diogenes_collection",
  "body": "body_cynic_simple",
  "hair": "hair_ancient_unkept",
  "eyes": "eyes_skeptical",
  "accessories": ["cloak_tattered", "lantern", "barrel"],
  "colors": {
    "skin": "#d4a574",
    "hair": "#8b6f47",
    "eyes": "#4a5568"
  },
  "metadata": {
    "created_at": "2026-01-22T00:00:00Z",
    "updated_at": "2026-01-22T00:00:00Z",
    "source": "manual",
    "description": "4th century BCE Cynic philosopher - deliberately simple, ascetic appearance with satirical accessories"
  }
}'::jsonb
WHERE id = 'a.diogenes';

-- Verify all board members have Woka data
DO $$
DECLARE
  board_count INTEGER;
  woka_count INTEGER;
BEGIN
  SELECT COUNT(*) INTO board_count
  FROM public.faculty
  WHERE id IN (
    'a.turing', 'a.katsushikaoi', 'a.davinci', 'a.darwin', 
    'a.avicenna', 'a.maryshelley', 'a.alkhwarizmi', 'a.plato', 
    'a.newton', 'a.henryrobert', 'a.diogenes'
  );
  
  SELECT COUNT(*) INTO woka_count
  FROM public.faculty
  WHERE id IN (
    'a.turing', 'a.katsushikaoi', 'a.davinci', 'a.darwin', 
    'a.avicenna', 'a.maryshelley', 'a.alkhwarizmi', 'a.plato', 
    'a.newton', 'a.henryrobert', 'a.diogenes'
  )
  AND woka IS NOT NULL;
  
  IF board_count = 11 AND woka_count = 11 THEN
    RAISE NOTICE '✅ All 11 board members have Woka designs configured';
  ELSE
    RAISE WARNING '⚠️ Expected 11 board members with Woka, found %/%', woka_count, board_count;
  END IF;
END $$;
