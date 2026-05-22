-- Simplified Corpus Processing Automation
-- Functions that work without requiring specific columns in faculty table

-- ============================================================================
-- VIEW: Faculty needing corpus processing (based on mapping file)
-- ============================================================================

-- View to easily see which faculty need corpus processing
-- Uses faculty_corpus_vectors table to check who has vectors
CREATE OR REPLACE VIEW faculty_needing_corpus_processing AS
SELECT 
  f.id,
  f.name,
  COALESCE((SELECT COUNT(*) FROM public.faculty_corpus_vectors WHERE faculty_id = f.id), 0) as vector_count,
  CASE 
    WHEN EXISTS (
      SELECT 1 FROM public.faculty_corpus_vectors WHERE faculty_id = f.id
    ) THEN false
    ELSE true
  END as needs_processing
FROM public.faculty f
WHERE 
  -- Faculty that might have corpus (we check via corpus-directory-mapping.json in scripts)
  -- This view shows all faculty - filtering happens in processing script via mapping
  true
  -- And doesn't have vectors yet
  AND NOT EXISTS (
    SELECT 1 FROM public.faculty_corpus_vectors WHERE faculty_id = f.id
  )
ORDER BY f.name;

COMMENT ON VIEW faculty_needing_corpus_processing IS 
  'View showing faculty without vectors. Use corpus-directory-mapping.json to determine which actually have corpus.';

-- ============================================================================
-- HELPER FUNCTION: Get faculty corpus processing status
-- ============================================================================

CREATE OR REPLACE FUNCTION get_all_faculty_corpus_status()
RETURNS TABLE (
  faculty_id text,
  faculty_name text,
  has_vectors boolean,
  vector_count bigint,
  last_processed timestamptz
) AS $$
BEGIN
  RETURN QUERY
  SELECT 
    f.id,
    f.name,
    EXISTS(SELECT 1 FROM public.faculty_corpus_vectors WHERE faculty_id = f.id) as has_vectors,
    COALESCE(
      (SELECT COUNT(*) FROM public.faculty_corpus_vectors WHERE faculty_id = f.id),
      0
    ) as vector_count,
    (SELECT MAX(updated_at) FROM public.faculty_corpus_vectors WHERE faculty_id = f.id) as last_processed
  FROM public.faculty f
  WHERE 
    -- Only return faculty that have vectors (or use mapping file to filter)
    EXISTS(SELECT 1 FROM public.faculty_corpus_vectors WHERE faculty_id = f.id)
  ORDER BY f.name;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION get_all_faculty_corpus_status IS 
  'Get corpus processing status for faculty with vectors. Use check-and-process-new-faculty-corpus.ts to find unprocessed faculty via mapping.';
