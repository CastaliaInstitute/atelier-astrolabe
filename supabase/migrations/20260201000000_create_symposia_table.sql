-- ============================================================================
-- Create symposia table for data-driven symposium management
-- ============================================================================
-- This table stores all symposium definitions and metadata
-- Used by the symposia static site to dynamically render symposium listings
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.symposia (
  id TEXT PRIMARY KEY,
  slug TEXT UNIQUE NOT NULL,
  title TEXT NOT NULL,
  title_native TEXT,
  title_translation TEXT,
  description TEXT,
  type TEXT NOT NULL CHECK (type IN ('symposium', 'course')),
  division TEXT,
  season TEXT,
  level TEXT,
  price_usd NUMERIC DEFAULT 0,
  
  -- Symposium-specific fields
  faculty TEXT[] DEFAULT '{}',
  heretic TEXT,
  session_count INTEGER,
  speaking_minutes INTEGER DEFAULT 10,
  central_question TEXT,
  central_question_native TEXT,
  themes TEXT[] DEFAULT '{}',
  setting TEXT,
  featured BOOLEAN DEFAULT false,
  image_url TEXT,
  style_theme TEXT CHECK (style_theme IN ('default', 'persian', 'greek', 'gothic', 'modern')),
  
  -- Extended detail fields
  tagline TEXT,
  overview TEXT,
  outcomes TEXT[] DEFAULT '{}',
  format_duration TEXT,
  format_cadence TEXT,
  format_modality TEXT,
  
  -- Metadata
  created_at TIMESTAMPTZ DEFAULT NOW(),
  updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- Create index for common queries
CREATE INDEX IF NOT EXISTS idx_symposia_slug ON public.symposia(slug);
CREATE INDEX IF NOT EXISTS idx_symposia_type ON public.symposia(type);
CREATE INDEX IF NOT EXISTS idx_symposia_featured ON public.symposia(featured);
CREATE INDEX IF NOT EXISTS idx_symposia_level ON public.symposia(level);

-- Enable RLS
ALTER TABLE public.symposia ENABLE ROW LEVEL SECURITY;

-- Allow public read access
CREATE POLICY "Allow public read access to symposia"
  ON public.symposia
  FOR SELECT
  USING (true);

-- Update timestamp trigger
CREATE OR REPLACE FUNCTION update_symposia_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_symposia_updated_at
  BEFORE UPDATE ON public.symposia
  FOR EACH ROW
  EXECUTE FUNCTION update_symposia_updated_at();

-- ============================================================================
-- Insert existing symposia
-- ============================================================================

-- 1. Iranian Symposium (Ayandeh-ye Iran)
INSERT INTO public.symposia (
  id,
  slug,
  title,
  title_native,
  title_translation,
  description,
  type,
  division,
  season,
  level,
  price_usd,
  faculty,
  heretic,
  session_count,
  speaking_minutes,
  central_question,
  central_question_native,
  themes,
  setting,
  featured,
  style_theme
) VALUES (
  'ayandeh-ye-iran',
  'ayandeh-ye-iran',
  'Symposion-e Āyandeh-ye Irān',
  'سمپوزیون آینده‌ی ایران',
  'Symposium on the Future of Iran',
  'Seven towering figures of Persian intellectual heritage gather in a timeless space to contemplate the destiny of their civilization. Epic poets, mystic seers, scientists, and philosophers—each brings their distinct voice to the eternal question of Iran''s future.',
  'symposium',
  'Persian Intellectual Heritage',
  'Timeless',
  'advanced',
  0,
  ARRAY['a-ferdowsi', 'a-saadi', 'a-rumi', 'a-avicenna', 'a-albiruni', 'a-khayyam'],
  'a-hafez',
  7,
  7,
  'What future can be imagined for Iran? And what role does our intellectual heritage play in shaping it?',
  'چه آینده‌ای برای ایران متصور است؟',
  ARRAY[
    'Identity & Continuity',
    'Faith & Reason',
    'Justice & Power',
    'Iran & the World',
    'Youth & Inheritance',
    'Language & Soul',
    'Hope & Realism'
  ],
  'A circular chamber, its walls inscribed with verses from the Shahnameh, the Masnavi, and the Rubaiyat. At the center, a fire burns in a bronze brazier—not consuming, only illuminating. Seven seats arranged in a circle. Time is suspended; the speakers appear as they wished to be remembered, in the fullness of their powers.',
  true,
  'persian'
) ON CONFLICT (slug) DO UPDATE SET
  title = EXCLUDED.title,
  title_native = EXCLUDED.title_native,
  title_translation = EXCLUDED.title_translation,
  description = EXCLUDED.description,
  updated_at = NOW();

-- 2. Absinthe Symposium
INSERT INTO public.symposia (
  id,
  slug,
  title,
  title_translation,
  description,
  type,
  division,
  season,
  level,
  price_usd,
  faculty,
  heretic,
  session_count,
  speaking_minutes,
  central_question,
  themes,
  setting,
  featured,
  style_theme
) VALUES (
  'absinthe',
  'absinthe',
  'Symposion of the Green Fairy',
  'Symposium on Absinthe',
  'Seven witnesses to absinthe gather—those who lived with it, loved it, abused it, ritualized it, and made art or magic through it. Not theorists, but practitioners. The Green Fairy gets her day in court, with voices from the canvas, the ritual chamber, the café, the laboratory, and the wreckage.',
  'symposium',
  'Intoxication & Altered States',
  'Timeless',
  'advanced',
  0,
  ARRAY['a.gogh', 'a.crowley', 'a.wilde', 'a.toulouse-lautrec', 'a.pasteur', 'a.foucault'],
  'a.verlaine',
  7,
  7,
  'What is absinthe? A substance, a symbol, a scapegoat, or a technology of transformation?',
  ARRAY[
    'Perception & Altered States',
    'Ritual & Intention',
    'Performance & Social Theater',
    'Everyday Practice',
    'Science & Substance',
    'Power & Prohibition',
    'Ruin & Redemption'
  ],
  'A fin-de-siècle Parisian café, the hour suspended between twilight and midnight. Green-tinted light filters through absinthe glasses arranged on marble tables. Seven figures gather—artists, magicians, scientists, poets—each bearing witness to the Green Fairy from their own lived experience. The air carries the scent of anise and wormwood, and time itself seems to louche.',
  true,
  'gothic'
) ON CONFLICT (slug) DO UPDATE SET
  title = EXCLUDED.title,
  title_translation = EXCLUDED.title_translation,
  description = EXCLUDED.description,
  updated_at = NOW();

-- 3. Buddhist Symposium (Attachment and Sangha)
INSERT INTO public.symposia (
  id,
  slug,
  title,
  description,
  type,
  division,
  season,
  level,
  price_usd,
  faculty,
  heretic,
  session_count,
  speaking_minutes,
  central_question,
  themes,
  setting,
  featured,
  style_theme,
  tagline,
  overview
) VALUES (
  'attachment-and-sangha',
  'attachment-and-sangha',
  'Attachment and Sangha',
  'Six Buddhist masters and one existential prosecutor examine the paradox: if attachment is the root of suffering, why do Buddhists build communities?',
  'symposium',
  'Buddhist Philosophy',
  'Timeless',
  'advanced',
  0,
  ARRAY['a-gautama-buddha', 'a-nagarjuna', 'a-vasubandhu', 'a-dogen', 'a-ashoka'],
  'a-simone-weil',
  7,
  7,
  'Attachment Is the Root of Suffering—So Why Do Buddhists Build Communities?',
  ARRAY[
    'Attachment & Non-Attachment',
    'Community & Solitude',
    'Form & Emptiness',
    'Practice & Theory',
    'Individual & Collective',
    'Scaling Compassion',
    'The Paradox of Refuge'
  ],
  'A timeless space where the constraints of history are suspended. Seven figures gather—six Buddhist masters spanning two and a half millennia, and one existential philosopher who presses Buddhism where it hurts most: attention, affliction, and the risk of consolation.',
  true,
  'default',
  'Attachment Is the Root of Suffering—So Why Do Buddhists Build Communities?',
  'In a timeless space where the constraints of history are suspended, seven towering figures—six Buddhist masters and one existential prosecutor—gather to confront the most fundamental paradox of Buddhist practice: if attachment is the root of suffering, why do Buddhists build communities?'
) ON CONFLICT (slug) DO UPDATE SET
  title = EXCLUDED.title,
  description = EXCLUDED.description,
  updated_at = NOW();
