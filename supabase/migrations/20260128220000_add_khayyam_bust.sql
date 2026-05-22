-- Add Omar Khayyam bust URLs
-- Note: Faculty record should exist (a.khayyam) before this runs

UPDATE public.faculty 
SET bust_url = '/busts/khayyam/bust.png',
    bust_frontal_url = '/busts/khayyam/bust_frontal.png'
WHERE id = 'a.khayyam';
