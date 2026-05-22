-- Add news topics field to faculty table for personalized RSS feeds
-- This allows faculty to specify topics they want to track for their morning News & Signals ritual

ALTER TABLE public.faculty 
ADD COLUMN IF NOT EXISTS news_topics text[] DEFAULT '{}';

-- Add index for efficient querying
CREATE INDEX IF NOT EXISTS idx_faculty_news_topics 
ON public.faculty USING GIN(news_topics) 
WHERE news_topics IS NOT NULL AND array_length(news_topics, 1) > 0;

-- Add comment
COMMENT ON COLUMN public.faculty.news_topics IS 
'Array of news topics/keywords that this faculty member wants to track. Used for personalized RSS feed aggregation in the News & Signals ritual. Examples: ["AI", "philosophy", "quantum computing", "climate change"]';
