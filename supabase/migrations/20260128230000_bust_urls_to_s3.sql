-- Update bust URLs to use S3 public bucket
-- S3 URL format: https://inquiry-institute-assets.s3.amazonaws.com/busts/{slug}/{filename}

-- Update angled busts (bust_url)
UPDATE public.faculty 
SET bust_url = REPLACE(bust_url, '/busts/', 'https://inquiry-institute-assets.s3.amazonaws.com/busts/')
WHERE bust_url LIKE '/busts/%';

-- Update frontal busts (bust_frontal_url)
UPDATE public.faculty 
SET bust_frontal_url = REPLACE(bust_frontal_url, '/busts/', 'https://inquiry-institute-assets.s3.amazonaws.com/busts/')
WHERE bust_frontal_url LIKE '/busts/%';

-- Also add Omar Khayyam if not already done
UPDATE public.faculty 
SET bust_url = 'https://inquiry-institute-assets.s3.amazonaws.com/busts/khayyam/bust.png',
    bust_frontal_url = 'https://inquiry-institute-assets.s3.amazonaws.com/busts/khayyam/bust_frontal.png'
WHERE id = 'a.khayyam';
