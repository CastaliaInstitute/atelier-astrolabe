-- Add Missing MHH Adjunct Faculty to arc_faculty
-- This ensures all required faculty are linked to the More Human Than Human arc

-- ============================================================================
-- ADD MISSING FACULTY (if not already present)
-- ============================================================================

DO $$
DECLARE
    v_arc_id UUID;
    v_added_count INTEGER := 0;
BEGIN
    -- Get the MHH arc ID
    SELECT id INTO v_arc_id 
    FROM inquiry_arcs 
    WHERE slug = 'more-human-than-human';
    
    IF v_arc_id IS NULL THEN
        RAISE EXCEPTION 'More Human Than Human arc not found';
    END IF;
    
    -- Add a-arendt as core (if not exists)
    IF NOT EXISTS (
        SELECT 1 FROM arc_faculty 
        WHERE arc_id = v_arc_id AND faculty_slug = 'a-arendt'
    ) THEN
        INSERT INTO arc_faculty (arc_id, faculty_slug, role, focus_area)
        VALUES (v_arc_id, 'a-arendt', 'core', 'Judgment, banality, responsibility');
        v_added_count := v_added_count + 1;
        RAISE NOTICE 'Added a-arendt as core faculty';
    ELSE
        RAISE NOTICE 'a-arendt already exists in arc_faculty';
    END IF;
    
    -- Add a-shannon as core (if not exists)
    IF NOT EXISTS (
        SELECT 1 FROM arc_faculty 
        WHERE arc_id = v_arc_id AND faculty_slug = 'a-shannon'
    ) THEN
        INSERT INTO arc_faculty (arc_id, faculty_slug, role, focus_area)
        VALUES (v_arc_id, 'a-shannon', 'core', 'Information vs meaning');
        v_added_count := v_added_count + 1;
        RAISE NOTICE 'Added a-shannon as core faculty';
    ELSE
        RAISE NOTICE 'a-shannon already exists in arc_faculty';
    END IF;
    
    -- Add a-foucault-soci as core (if not exists)
    IF NOT EXISTS (
        SELECT 1 FROM arc_faculty 
        WHERE arc_id = v_arc_id AND faculty_slug = 'a-foucault-soci'
    ) THEN
        INSERT INTO arc_faculty (arc_id, faculty_slug, role, focus_area)
        VALUES (v_arc_id, 'a-foucault-soci', 'core', 'Classification, power, surveillance');
        v_added_count := v_added_count + 1;
        RAISE NOTICE 'Added a-foucault-soci as core faculty';
    ELSE
        RAISE NOTICE 'a-foucault-soci already exists in arc_faculty';
    END IF;
    
    -- Add a-nabokov as guest (if not exists)
    IF NOT EXISTS (
        SELECT 1 FROM arc_faculty 
        WHERE arc_id = v_arc_id AND faculty_slug = 'a-nabokov'
    ) THEN
        INSERT INTO arc_faculty (arc_id, faculty_slug, role, focus_area)
        VALUES (v_arc_id, 'a-nabokov', 'guest', 'Interpretation, misreading, Pale Fire');
        v_added_count := v_added_count + 1;
        RAISE NOTICE 'Added a-nabokov as guest faculty';
    ELSE
        RAISE NOTICE 'a-nabokov already exists in arc_faculty';
    END IF;
    
    RAISE NOTICE 'Migration complete. Added % new faculty assignments.', v_added_count;
    
END $$;

-- ============================================================================
-- VERIFICATION: Show all MHH faculty
-- ============================================================================

SELECT 
    af.faculty_slug,
    af.role,
    af.focus_area,
    f.name as faculty_name,
    f.slug as faculty_slug_dot,
    CASE 
        WHEN f.id IS NULL THEN '⚠️ NOT IN FACULTY TABLE'
        ELSE '✅ EXISTS'
    END as faculty_table_status
FROM arc_faculty af
LEFT JOIN faculty f ON f.slug = REPLACE(af.faculty_slug, '-', '.')
WHERE af.arc_id = (SELECT id FROM inquiry_arcs WHERE slug = 'more-human-than-human')
ORDER BY 
    CASE af.role 
        WHEN 'lead' THEN 1
        WHEN 'core' THEN 2
        WHEN 'guest' THEN 3
        ELSE 4
    END,
    af.faculty_slug;
