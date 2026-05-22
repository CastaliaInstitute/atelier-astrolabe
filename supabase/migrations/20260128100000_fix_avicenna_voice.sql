-- Fix Avicenna voice assignment
-- The original migration referenced 'a.ibnsina' but the ID is 'a.avicenna'

UPDATE public.faculty SET 
  voice_id = 'ar-SA-HamedNeural',
  voice_language = 'ar-SA',
  voice_accent = 'Arabic (Saudi)',
  voice_rate = 0.9
WHERE id = 'a.avicenna' AND (voice_id IS NULL OR voice_id NOT LIKE 'ar-%');

-- Also fix Ibn al-Haytham if needed
UPDATE public.faculty SET 
  voice_id = 'ar-EG-ShakirNeural',
  voice_language = 'ar-EG',
  voice_accent = 'Arabic (Egyptian)',
  voice_rate = 0.9
WHERE id = 'a.ibnalhaytham' AND (voice_id IS NULL OR voice_id NOT LIKE 'ar-%');
