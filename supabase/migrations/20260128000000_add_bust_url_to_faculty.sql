-- Add bust_url column to faculty table for 2.5D marble bust animations
-- These busts are generated via the reliquary/talking-bust pipeline

ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS bust_url text;

-- Add index for bust lookups
CREATE INDEX IF NOT EXISTS idx_faculty_bust_url 
ON public.faculty(bust_url) 
WHERE bust_url IS NOT NULL;

-- Comment explaining the column
COMMENT ON COLUMN public.faculty.bust_url IS 'URL to 2.5D marble bust image (768x1024 PNG with transparency). Used for animated faculty displays via the talking-bust system.';

-- Update directors with bust URLs
-- Format: /busts/{slug}/bust.png (will be served from public assets)

UPDATE public.faculty SET bust_url = '/busts/adalovelace/bust.png' WHERE id = 'a.adalovelace' OR id = 'a.lovelace';
UPDATE public.faculty SET bust_url = '/busts/alkhwarizmi/bust.png' WHERE id = 'a.alkhwarizmi';
UPDATE public.faculty SET bust_url = '/busts/avicenna/bust.png' WHERE id = 'a.avicenna' OR id = 'a.ibnsina';
UPDATE public.faculty SET bust_url = '/busts/confucius/bust.png' WHERE id = 'a.confucius';
UPDATE public.faculty SET bust_url = '/busts/diogenes/bust.png' WHERE id = 'a.diogenes';
UPDATE public.faculty SET bust_url = '/busts/ibnalhaytham/bust.png' WHERE id = 'a.ibnalhaytham';
UPDATE public.faculty SET bust_url = '/busts/katsushikaoi/bust.png' WHERE id = 'a.katsushikaoi' OR id = 'a.oi';
UPDATE public.faculty SET bust_url = '/busts/leonardo/bust.png' WHERE id = 'a.leonardo' OR id = 'a.davinci';
UPDATE public.faculty SET bust_url = '/busts/mariamerian/bust.png' WHERE id = 'a.mariamerian' OR id = 'a.merian';
UPDATE public.faculty SET bust_url = '/busts/maryshelley/bust.png' WHERE id = 'a.maryshelley' OR id = 'a.shelley';
UPDATE public.faculty SET bust_url = '/busts/zhuangzi/bust.png' WHERE id = 'a.zhuangzi';

-- More Human Than Human faculty
UPDATE public.faculty SET bust_url = '/busts/turing/bust.png' WHERE id = 'a.turing';
UPDATE public.faculty SET bust_url = '/busts/dick/bust.png' WHERE id = 'a.dick';
UPDATE public.faculty SET bust_url = '/busts/weizenbaum/bust.png' WHERE id = 'a.weizenbaum';
UPDATE public.faculty SET bust_url = '/busts/shannon/bust.png' WHERE id = 'a.shannon';
UPDATE public.faculty SET bust_url = '/busts/arendt/bust.png' WHERE id = 'a.arendt';
UPDATE public.faculty SET bust_url = '/busts/hume/bust.png' WHERE id = 'a.hume';
UPDATE public.faculty SET bust_url = '/busts/foucault/bust.png' WHERE id = 'a.foucault';
UPDATE public.faculty SET bust_url = '/busts/wiener/bust.png' WHERE id = 'a.wiener';
UPDATE public.faculty SET bust_url = '/busts/nabokov/bust.png' WHERE id = 'a.nabokov';
UPDATE public.faculty SET bust_url = '/busts/barthes/bust.png' WHERE id = 'a.barthes';
UPDATE public.faculty SET bust_url = '/busts/mccarthy/bust.png' WHERE id = 'a.mccarthy';
UPDATE public.faculty SET bust_url = '/busts/butler/bust.png' WHERE id = 'a.butler';
UPDATE public.faculty SET bust_url = '/busts/skinner/bust.png' WHERE id = 'a.skinner';
UPDATE public.faculty SET bust_url = '/busts/friedman/bust.png' WHERE id = 'a.friedman';
UPDATE public.faculty SET bust_url = '/busts/posner/bust.png' WHERE id = 'a.posner';
