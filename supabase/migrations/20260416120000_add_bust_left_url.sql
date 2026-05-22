-- Left 3/4 marble bust (complements bust_right_url / bust.png and frontal / bust_frontal.png).
ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS bust_left_url text;

COMMENT ON COLUMN public.faculty.bust_left_url IS 'URL to left-facing (3/4 view) marble bust. Together with bust_frontal_url (enter/center) and bust_right_url (right 3/4).';

CREATE INDEX IF NOT EXISTS idx_faculty_bust_left_url
ON public.faculty(bust_left_url)
WHERE bust_left_url IS NOT NULL;
