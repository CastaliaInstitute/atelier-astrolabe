-- Migration: Add voice specification fields to faculty table
-- This allows each faculty member to have a unique TTS voice configuration
-- Used by Matrix TTS integration and other voice services

-- Add voice specification columns to faculty table
ALTER TABLE faculty ADD COLUMN IF NOT EXISTS voice_id TEXT;
ALTER TABLE faculty ADD COLUMN IF NOT EXISTS voice_rate DECIMAL(3,2) DEFAULT 1.0;
ALTER TABLE faculty ADD COLUMN IF NOT EXISTS voice_pitch INTEGER DEFAULT 0;
ALTER TABLE faculty ADD COLUMN IF NOT EXISTS voice_volume INTEGER DEFAULT 100;
ALTER TABLE faculty ADD COLUMN IF NOT EXISTS voice_language TEXT;
ALTER TABLE faculty ADD COLUMN IF NOT EXISTS voice_accent TEXT;

-- Add comments for documentation
COMMENT ON COLUMN faculty.voice_id IS 'Microsoft Edge TTS voice ID (e.g., en-US-GuyNeural, de-DE-ConradNeural)';
COMMENT ON COLUMN faculty.voice_rate IS 'Speech rate multiplier (0.5-2.0, default 1.0)';
COMMENT ON COLUMN faculty.voice_pitch IS 'Pitch adjustment in Hz (-50 to +50, default 0)';
COMMENT ON COLUMN faculty.voice_volume IS 'Volume percentage (0-100, default 100)';
COMMENT ON COLUMN faculty.voice_language IS 'Voice language code (e.g., en-US, de-DE, el-GR)';
COMMENT ON COLUMN faculty.voice_accent IS 'Human-readable accent description (e.g., British, German, Greek)';

-- Create index for voice lookups
CREATE INDEX IF NOT EXISTS idx_faculty_voice_id ON faculty(voice_id) WHERE voice_id IS NOT NULL;

-- Update existing faculty with appropriate voice assignments based on nationality/era
-- Ancient Greek philosophers
UPDATE faculty SET 
  voice_id = 'el-GR-NestorNeural',
  voice_language = 'el-GR',
  voice_accent = 'Greek',
  voice_rate = 0.95
WHERE slug IN ('a.plato', 'a.aristotle', 'a.socrates') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'el-GR-AthinaNeural',
  voice_language = 'el-GR',
  voice_accent = 'Greek',
  voice_rate = 0.95
WHERE slug = 'a.hypatia' AND voice_id IS NULL;

-- German-speaking scholars
UPDATE faculty SET 
  voice_id = 'de-DE-ConradNeural',
  voice_language = 'de-DE',
  voice_accent = 'German',
  voice_rate = 0.9
WHERE slug IN ('a.einstein', 'a.kant', 'a.nietzsche', 'a.heidegger', 'a.euler', 'a.gauss', 'a.cantor', 'a.heisenberg', 'a.weber') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'de-AT-JonasNeural',
  voice_language = 'de-AT',
  voice_accent = 'Austrian German',
  voice_rate = 0.9
WHERE slug IN ('a.steiner', 'a.mendel') AND voice_id IS NULL;

-- British scholars
UPDATE faculty SET 
  voice_id = 'en-GB-RyanNeural',
  voice_language = 'en-GB',
  voice_accent = 'British RP',
  voice_rate = 0.95
WHERE slug IN ('a.newton', 'a.darwin', 'a.shakespeare', 'a.hawking', 'a.turing', 'a.smith', 'a.gibbon', 'a.maxwell', 'a.churchill') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'en-GB-SoniaNeural',
  voice_language = 'en-GB',
  voice_accent = 'British',
  voice_rate = 0.95
WHERE slug IN ('a.woolf', 'a.ada', 'a.maryshelley', 'a.franklin') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'en-GB-ThomasNeural',
  voice_language = 'en-GB',
  voice_accent = 'British',
  voice_rate = 0.95
WHERE slug IN ('a.dickens', 'a.keynes', 'a.crick', 'a.toynbee', 'a.dalton') AND voice_id IS NULL;

-- American scholars
UPDATE faculty SET 
  voice_id = 'en-US-DavisNeural',
  voice_language = 'en-US',
  voice_accent = 'American',
  voice_rate = 1.0
WHERE slug IN ('a.tesla', 'a.knuth', 'a.lincoln', 'a.thoreau', 'a.rothko') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'en-US-TonyNeural',
  voice_language = 'en-US',
  voice_accent = 'American',
  voice_rate = 1.05
WHERE slug = 'a.feynman' AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'en-US-JennyNeural',
  voice_language = 'en-US',
  voice_accent = 'American',
  voice_rate = 0.9
WHERE slug IN ('a.morrison', 'a.carson') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'en-US-AriaNeural',
  voice_language = 'en-US',
  voice_accent = 'American',
  voice_rate = 1.0
WHERE slug = 'a.hopper' AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'en-US-GuyNeural',
  voice_language = 'en-US',
  voice_accent = 'American',
  voice_rate = 1.0
WHERE slug IN ('a.shannon', 'a.diamond') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'en-US-JasonNeural',
  voice_language = 'en-US',
  voice_accent = 'American',
  voice_rate = 0.95
WHERE slug IN ('a.mccarthy', 'a.pauling', 'a.mcneill', 'a.rawls', 'a.vonneumann', 'a.washington') AND voice_id IS NULL;

-- French scholars
UPDATE faculty SET 
  voice_id = 'fr-FR-HenriNeural',
  voice_language = 'fr-FR',
  voice_accent = 'French',
  voice_rate = 0.95
WHERE slug IN ('a.sartre', 'a.foucault', 'a.braudel', 'a.lavoisier', 'a.pasteur', 'a.monet') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'fr-FR-DeniseNeural',
  voice_language = 'fr-FR',
  voice_accent = 'French',
  voice_rate = 0.95
WHERE slug = 'a.curie' AND voice_id IS NULL;

-- Italian scholars
UPDATE faculty SET 
  voice_id = 'it-IT-DiegoNeural',
  voice_language = 'it-IT',
  voice_accent = 'Italian',
  voice_rate = 0.9
WHERE slug IN ('a.davinci', 'a.michelangelo', 'a.machiavelli', 'a.tacitus', 'a.thomasaquinas') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'it-IT-ElsaNeural',
  voice_language = 'it-IT',
  voice_accent = 'Italian',
  voice_rate = 0.95
WHERE slug = 'a.montessori' AND voice_id IS NULL;

-- Spanish scholars
UPDATE faculty SET 
  voice_id = 'es-ES-AlvaroNeural',
  voice_language = 'es-ES',
  voice_accent = 'Castilian Spanish',
  voice_rate = 0.85
WHERE slug IN ('a.borges', 'a.picasso') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'es-MX-DaliaNeural',
  voice_language = 'es-MX',
  voice_accent = 'Mexican Spanish',
  voice_rate = 0.95
WHERE slug IN ('a.kahlo', 'a.sorjuana', 'a.anzaldua') AND voice_id IS NULL;

-- Irish scholars
UPDATE faculty SET 
  voice_id = 'en-IE-ConnorNeural',
  voice_language = 'en-IE',
  voice_accent = 'Irish',
  voice_rate = 0.9
WHERE slug IN ('a.joyce', 'a.boyle') AND voice_id IS NULL;

-- Dutch scholars
UPDATE faculty SET 
  voice_id = 'nl-NL-MaartenNeural',
  voice_language = 'nl-NL',
  voice_accent = 'Dutch',
  voice_rate = 0.95
WHERE slug = 'a.dijkstra' AND voice_id IS NULL;

-- Indian scholars (English)
UPDATE faculty SET 
  voice_id = 'en-IN-PrabhatNeural',
  voice_language = 'en-IN',
  voice_accent = 'Indian English',
  voice_rate = 0.95
WHERE slug IN ('a.ramanujan', 'a.tagore', 'a.sen') AND voice_id IS NULL;

-- Hindi voice for ancient Indian sages
UPDATE faculty SET 
  voice_id = 'hi-IN-MadhurNeural',
  voice_language = 'hi-IN',
  voice_accent = 'Hindi',
  voice_rate = 0.85
WHERE slug IN ('a.nagarjuna', 'a.upanishadicsage') AND voice_id IS NULL;

-- Chinese scholars
UPDATE faculty SET 
  voice_id = 'zh-CN-YunxiNeural',
  voice_language = 'zh-CN',
  voice_accent = 'Mandarin Chinese',
  voice_rate = 0.9
WHERE slug IN ('a.suntzu', 'a.wangyangming', 'a.confucius', 'a.chengzhulineage') AND voice_id IS NULL;

-- Japanese scholars
UPDATE faculty SET 
  voice_id = 'ja-JP-NanamiNeural',
  voice_language = 'ja-JP',
  voice_accent = 'Japanese',
  voice_rate = 0.9
WHERE slug = 'a.murasaki' AND voice_id IS NULL;

-- Russian scholars
UPDATE faculty SET 
  voice_id = 'ru-RU-DmitryNeural',
  voice_language = 'ru-RU',
  voice_accent = 'Russian',
  voice_rate = 0.95
WHERE slug IN ('a.mendeleev', 'a.kandinsky') AND voice_id IS NULL;

-- Arabic scholars
UPDATE faculty SET 
  voice_id = 'ar-SA-HamedNeural',
  voice_language = 'ar-SA',
  voice_accent = 'Arabic (Saudi)',
  voice_rate = 0.9
WHERE slug IN ('a.ibnsina', 'a.alkhwarizmi', 'a.alfarabi') AND voice_id IS NULL;

UPDATE faculty SET 
  voice_id = 'ar-EG-ShakirNeural',
  voice_language = 'ar-EG',
  voice_accent = 'Arabic (Egyptian)',
  voice_rate = 0.9
WHERE slug IN ('a.ibnkhaldun', 'a.usmandanfodio') AND voice_id IS NULL;

-- African diaspora scholars (using French for Martinican)
UPDATE faculty SET 
  voice_id = 'fr-FR-HenriNeural',
  voice_language = 'fr-FR',
  voice_accent = 'French (Martinican)',
  voice_rate = 1.0
WHERE slug IN ('a.fanon', 'a.aimecesaire') AND voice_id IS NULL;

-- South African voice for African scholars
UPDATE faculty SET 
  voice_id = 'en-ZA-LeahNeural',
  voice_language = 'en-ZA',
  voice_accent = 'South African English',
  voice_rate = 0.9
WHERE slug = 'a.nanasmau' AND voice_id IS NULL;

-- Default English voice for any remaining faculty
UPDATE faculty SET 
  voice_id = 'en-US-GuyNeural',
  voice_language = 'en-US',
  voice_accent = 'American (Default)',
  voice_rate = 1.0
WHERE voice_id IS NULL;

-- Create a view for easy voice lookup
CREATE OR REPLACE VIEW faculty_voices AS
SELECT 
  id,
  slug,
  name,
  surname,
  voice_id,
  voice_rate,
  voice_pitch,
  voice_volume,
  voice_language,
  voice_accent,
  COALESCE(name, '') || ' ' || COALESCE(surname, '') AS full_name
FROM faculty
WHERE voice_id IS NOT NULL;

-- Grant access to the view
GRANT SELECT ON faculty_voices TO anon, authenticated, service_role;
