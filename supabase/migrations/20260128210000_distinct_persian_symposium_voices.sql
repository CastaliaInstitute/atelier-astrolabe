-- Assign distinct, varied voices to Persian Symposium speakers
-- Using a mix of Persian (fa-IR) and Arabic (ar-) voices with rate/pitch variance
-- to make each speaker distinguishable

-- FERDOWSI - The Epic Voice
-- Use Persian male voice with slow, grave delivery
UPDATE public.faculty SET
  voice_id = 'fa-IR-FaridNeural',
  voice_language = 'fa-IR',
  voice_accent = 'Persian (Farsi) - Epic/Grave',
  voice_rate = 0.85,
  voice_pitch = -5
WHERE id = 'a.ferdowsi';

-- SA'DI - The Moral Compass
-- Use Persian female voice (Dilara) for variety - warm, measured
UPDATE public.faculty SET
  voice_id = 'fa-IR-DilaraNeural',
  voice_language = 'fa-IR',
  voice_accent = 'Persian (Farsi) - Wise/Warm',
  voice_rate = 0.92,
  voice_pitch = 0
WHERE id = 'a.saadi';

-- RUMI - The Universal Soul
-- Use Persian male with slightly elevated pitch and moderate pace - mystical
UPDATE public.faculty SET
  voice_id = 'fa-IR-FaridNeural',
  voice_language = 'fa-IR',
  voice_accent = 'Persian (Farsi) - Mystical/Elevated',
  voice_rate = 0.88,
  voice_pitch = 3
WHERE id = 'a.rumi';

-- AVICENNA (Ibn Sina) - The Rational Mind
-- Use Arabic Saudi voice - scholarly, precise, moderate pace
-- (He wrote primarily in Arabic; his philosophical works were in Arabic)
UPDATE public.faculty SET
  voice_id = 'ar-SA-HamedNeural',
  voice_language = 'ar-SA',
  voice_accent = 'Arabic (Saudi) - Scholarly/Precise',
  voice_rate = 0.90,
  voice_pitch = 0
WHERE id = 'a.avicenna';

-- AL-BIRUNI - The Scientific Eye
-- Use Arabic Egyptian voice - observational, methodical
-- (He wrote primarily in Arabic and conducted research across cultures)
UPDATE public.faculty SET
  voice_id = 'ar-EG-ShakirNeural',
  voice_language = 'ar-EG',
  voice_accent = 'Arabic (Egyptian) - Scientific/Methodical',
  voice_rate = 0.95,
  voice_pitch = -2
WHERE id = 'a.albiruni';

-- OMAR KHAYYAM - The Skeptical Mathematician
-- Use Persian male with slightly faster pace and lower pitch - witty, skeptical
UPDATE public.faculty SET
  voice_id = 'fa-IR-FaridNeural',
  voice_language = 'fa-IR',
  voice_accent = 'Persian (Farsi) - Skeptical/Wry',
  voice_rate = 0.98,
  voice_pitch = -3
WHERE id = 'a.khayyam';

-- HAFEZ - The Heretic
-- Use Persian male with varied pace - lyrical, provocative, the interrupter
UPDATE public.faculty SET
  voice_id = 'fa-IR-FaridNeural',
  voice_language = 'fa-IR',
  voice_accent = 'Persian (Farsi) - Lyrical/Provocative',
  voice_rate = 1.02,
  voice_pitch = 2
WHERE id = 'a.hafez';

-- Also update bust URLs for these speakers (from Supabase storage)
-- Using local paths that will be served from public/busts/
UPDATE public.faculty SET bust_url = '/busts/ferdowsi/bust.png' WHERE id = 'a.ferdowsi';
UPDATE public.faculty SET bust_url = '/busts/saadi/bust.png' WHERE id = 'a.saadi';
UPDATE public.faculty SET bust_url = '/busts/rumi/bust.png' WHERE id = 'a.rumi';
UPDATE public.faculty SET bust_url = '/busts/avicenna/bust.png' WHERE id = 'a.avicenna';
UPDATE public.faculty SET bust_url = '/busts/albiruni/bust.png' WHERE id = 'a.albiruni';
UPDATE public.faculty SET bust_url = '/busts/khayyam/bust.png' WHERE id = 'a.khayyam';
UPDATE public.faculty SET bust_url = '/busts/hafez/bust.png' WHERE id = 'a.hafez';

-- Also set frontal bust URLs
UPDATE public.faculty SET bust_frontal_url = '/busts/ferdowsi/bust_frontal.png' WHERE id = 'a.ferdowsi';
UPDATE public.faculty SET bust_frontal_url = '/busts/saadi/bust_frontal.png' WHERE id = 'a.saadi';
UPDATE public.faculty SET bust_frontal_url = '/busts/rumi/bust_frontal.png' WHERE id = 'a.rumi';
UPDATE public.faculty SET bust_frontal_url = '/busts/avicenna/bust_frontal.png' WHERE id = 'a.avicenna';
UPDATE public.faculty SET bust_frontal_url = '/busts/albiruni/bust_frontal.png' WHERE id = 'a.albiruni';
UPDATE public.faculty SET bust_frontal_url = '/busts/khayyam/bust_frontal.png' WHERE id = 'a.khayyam';
UPDATE public.faculty SET bust_frontal_url = '/busts/hafez/bust_frontal.png' WHERE id = 'a.hafez';
