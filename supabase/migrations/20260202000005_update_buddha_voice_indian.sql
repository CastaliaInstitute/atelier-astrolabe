-- ============================================================================
-- Update Buddha Voice to Indian Multilingual
-- ============================================================================
-- Change Buddha's voice from en-US-DavisNeural to en-IN-PrabhatNeural
-- which is an Indian English voice that can handle multilingual content
-- ============================================================================

UPDATE public.faculty 
SET 
  voice_id = 'en-IN-PrabhatNeural',
  voice_language = 'en-IN',
  voice_accent = 'Indian English, multilingual',
  updated_at = now()
WHERE id = 'a.gautama.buddha';

-- Also update the fallback voice in the component configuration
-- (This is handled in AttachmentAndSanghaPresentation.tsx)

-- Verification
DO $$
DECLARE
    v_voice_id TEXT;
    v_voice_lang TEXT;
BEGIN
    SELECT voice_id, voice_language 
    INTO v_voice_id, v_voice_lang
    FROM public.faculty 
    WHERE id = 'a.gautama.buddha';
    
    RAISE NOTICE '✅ Buddha voice updated: % (%)', v_voice_id, v_voice_lang;
    
    IF v_voice_id = 'en-IN-PrabhatNeural' THEN
        RAISE NOTICE '✅ Successfully updated to Indian multilingual voice';
    ELSE
        RAISE WARNING '⚠️  Voice not updated correctly';
    END IF;
END $$;
