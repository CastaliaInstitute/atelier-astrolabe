-- ============================================================================
-- Fix Attachment and Sangha Faculty IDs
-- ============================================================================
-- The edge function normalizes handles from dash to dot format (a-gautama-buddha -> a.gautama.buddha)
-- But we inserted faculty with dash IDs. This migration updates the IDs to dot format
-- to match what the edge function expects.
-- ============================================================================

-- First, update faculty_colleges foreign keys (must be done before updating faculty.id)
UPDATE public.faculty_colleges SET faculty_id = 'a.gautama.buddha' WHERE faculty_id = 'a-gautama-buddha';
UPDATE public.faculty_colleges SET faculty_id = 'a.ashoka' WHERE faculty_id = 'a-ashoka';
UPDATE public.faculty_colleges SET faculty_id = 'a.dogen' WHERE faculty_id = 'a-dogen';
UPDATE public.faculty_colleges SET faculty_id = 'a.simone.weil' WHERE faculty_id = 'a-simone-weil';
UPDATE public.faculty_colleges SET faculty_id = 'a.nagarjuna' WHERE faculty_id = 'a-nagarjuna' AND NOT EXISTS (SELECT 1 FROM public.faculty_colleges WHERE faculty_id = 'a.nagarjuna');
UPDATE public.faculty_colleges SET faculty_id = 'a.vasubandhu' WHERE faculty_id = 'a-vasubandhu' AND NOT EXISTS (SELECT 1 FROM public.faculty_colleges WHERE faculty_id = 'a.vasubandhu');
UPDATE public.faculty_colleges SET faculty_id = 'a.nietzsche' WHERE faculty_id = 'a-nietzsche' AND NOT EXISTS (SELECT 1 FROM public.faculty_colleges WHERE faculty_id = 'a.nietzsche');

-- Now update faculty IDs from dash format to dot format
UPDATE public.faculty SET id = 'a.gautama.buddha' WHERE id = 'a-gautama-buddha';
UPDATE public.faculty SET id = 'a.ashoka' WHERE id = 'a-ashoka';
UPDATE public.faculty SET id = 'a.dogen' WHERE id = 'a-dogen';
UPDATE public.faculty SET id = 'a.simone.weil' WHERE id = 'a-simone-weil';

-- Update slugs to match (keep dash format for URLs)
UPDATE public.faculty SET slug = 'a-gautama-buddha' WHERE id = 'a.gautama.buddha';
UPDATE public.faculty SET slug = 'a-ashoka' WHERE id = 'a.ashoka';
UPDATE public.faculty SET slug = 'a-dogen' WHERE id = 'a.dogen';
UPDATE public.faculty SET slug = 'a-simone-weil' WHERE id = 'a.simone.weil';

-- Also ensure the IDs that are already in dot format have correct slugs
UPDATE public.faculty SET slug = 'a-nagarjuna' WHERE id = 'a.nagarjuna' AND slug != 'a-nagarjuna';
UPDATE public.faculty SET slug = 'a-vasubandhu' WHERE id = 'a.vasubandhu' AND slug != 'a-vasubandhu';
UPDATE public.faculty SET slug = 'a-nietzsche' WHERE id = 'a.nietzsche' AND slug != 'a-nietzsche';

-- Verification
DO $$
DECLARE
    v_count INTEGER;
    v_names TEXT;
BEGIN
    SELECT COUNT(*), string_agg(id, ', ' ORDER BY id) 
    INTO v_count, v_names
    FROM public.faculty 
    WHERE id IN ('a.gautama.buddha', 'a.nagarjuna', 'a.vasubandhu', 'a.dogen', 'a.ashoka', 'a.simone.weil', 'a.nietzsche');
    
    RAISE NOTICE 'Attachment and Sangha Faculty (dot format): % of 7', v_count;
    RAISE NOTICE 'IDs: %', v_names;
END $$;
