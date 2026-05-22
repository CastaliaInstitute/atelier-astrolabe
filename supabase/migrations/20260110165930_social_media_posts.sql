-- Social Media Posts Table
-- Tracks posts to LinkedIn and Facebook for Inquirer articles

CREATE TABLE IF NOT EXISTS public.social_media_posts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  
  -- Article reference
  article_volume text NOT NULL,
  article_issue text NOT NULL,
  article_slug text NOT NULL,
  article_url text NOT NULL,
  article_title text NOT NULL,
  
  -- Platform
  platform text NOT NULL CHECK (platform IN ('linkedin', 'facebook')),
  
  -- Post status
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'posted', 'failed', 'skipped')),
  
  -- Post URLs (null until posted)
  post_url text, -- LinkedIn or Facebook post URL
  post_id text,  -- Platform-specific post ID
  
  -- Post metadata
  caption text, -- The caption that was posted
  posted_at timestamptz, -- When the post was created on the platform
  
  -- Error tracking
  error_message text,
  
  -- Timestamps
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_social_media_posts_article ON public.social_media_posts(article_volume, article_issue, article_slug);
CREATE INDEX IF NOT EXISTS idx_social_media_posts_platform ON public.social_media_posts(platform);
CREATE INDEX IF NOT EXISTS idx_social_media_posts_status ON public.social_media_posts(status);
CREATE UNIQUE INDEX IF NOT EXISTS idx_social_media_posts_unique ON public.social_media_posts(article_volume, article_issue, article_slug, platform);

-- Enable RLS
ALTER TABLE public.social_media_posts ENABLE ROW LEVEL SECURITY;

-- Policy: Anyone can read (public posts)
CREATE POLICY "Anyone can read social media posts"
  ON public.social_media_posts
  FOR SELECT
  USING (true);

-- Policy: Allow authenticated users to manage posts
-- TODO: Tighten this to require admin privileges once user_privileges table exists
-- For now, any authenticated user can manage posts (you can restrict this later)
CREATE POLICY "Authenticated users can manage social media posts"
  ON public.social_media_posts
  FOR ALL
  USING (auth.uid() IS NOT NULL)
  WITH CHECK (auth.uid() IS NOT NULL);

-- Function to update updated_at timestamp
CREATE OR REPLACE FUNCTION update_social_media_posts_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Trigger to auto-update updated_at
CREATE TRIGGER update_social_media_posts_updated_at
  BEFORE UPDATE ON public.social_media_posts
  FOR EACH ROW
  EXECUTE FUNCTION update_social_media_posts_updated_at();
