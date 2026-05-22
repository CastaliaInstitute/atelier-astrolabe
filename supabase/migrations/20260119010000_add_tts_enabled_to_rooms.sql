-- Add TTS enabled column to matrix_rooms table
-- When true, faculty responses in this room will be spoken aloud via TTS

ALTER TABLE public.matrix_rooms
ADD COLUMN IF NOT EXISTS tts_enabled BOOLEAN DEFAULT false;

COMMENT ON COLUMN public.matrix_rooms.tts_enabled IS 'When true, faculty responses are also sent as audio messages via TTS';

-- Enable TTS for the Board of Directors room (if it exists)
UPDATE public.matrix_rooms
SET tts_enabled = true
WHERE name ILIKE '%board%' OR name ILIKE '%directors%';
