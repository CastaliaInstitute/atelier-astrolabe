-- Add corpus_type column to faculty_corpus_vectors for distinguishing primary vs secondary works
-- This enables separate RAG retrieval for "speaking AS" vs "speaking ABOUT" the faculty member

-- ============================================================================
-- ADD CORPUS_TYPE COLUMN
-- ============================================================================

-- Add corpus_type column to distinguish works BY vs works ABOUT
ALTER TABLE public.faculty_corpus_vectors
ADD COLUMN IF NOT EXISTS corpus_type text DEFAULT 'works_by';

-- Add constraint for valid corpus types
ALTER TABLE public.faculty_corpus_vectors
ADD CONSTRAINT chk_corpus_type CHECK (corpus_type IN ('works_by', 'works_about', 'general'));

-- Create index on corpus_type for filtering
CREATE INDEX IF NOT EXISTS idx_faculty_corpus_vectors_corpus_type 
ON public.faculty_corpus_vectors (faculty_id, corpus_type);

COMMENT ON COLUMN public.faculty_corpus_vectors.corpus_type IS 'Type of corpus: works_by (primary sources written by faculty), works_about (secondary literature about faculty), general (mixed/unclassified)';

-- ============================================================================
-- UPDATE RAG FUNCTION TO SUPPORT CORPUS TYPE FILTERING
-- ============================================================================

-- Enhanced function that supports corpus_type filtering
CREATE OR REPLACE FUNCTION get_faculty_rag_context(
  faculty_id_param text,
  query_embedding text,
  match_threshold float DEFAULT 0.7,
  match_count int DEFAULT 5,
  corpus_type_param text DEFAULT NULL -- NULL means all types
)
RETURNS TABLE (
  chunk_text text,
  similarity float,
  metadata jsonb,
  source_file text,
  corpus_type text
) AS $$
DECLARE
  embedding_vector vector(1536);
BEGIN
  -- Cast text to vector (format: "[0.1,0.2,...]")
  embedding_vector := query_embedding::vector;
  
  RETURN QUERY
  SELECT 
    v.chunk_text,
    1 - (v.embedding <=> embedding_vector) as similarity,
    v.metadata,
    v.source_file,
    v.corpus_type
  FROM public.faculty_corpus_vectors v
  WHERE v.faculty_id = faculty_id_param
    AND v.embedding IS NOT NULL
    AND (corpus_type_param IS NULL OR v.corpus_type = corpus_type_param)
    AND 1 - (v.embedding <=> embedding_vector) >= match_threshold
  ORDER BY v.embedding <=> embedding_vector
  LIMIT match_count;
END;
$$ LANGUAGE plpgsql;

-- ============================================================================
-- NEW FUNCTION: HYBRID RAG CONTEXT (PRIMARY + SECONDARY)
-- ============================================================================

-- Function to get hybrid RAG context with weighted blending
-- For agent responses, we can retrieve both primary works (to speak AS the author)
-- and secondary literature (for biographical/contextual knowledge)
CREATE OR REPLACE FUNCTION get_faculty_hybrid_rag_context(
  faculty_id_param text,
  query_embedding text,
  match_threshold float DEFAULT 0.6,
  primary_count int DEFAULT 4,    -- Works BY the author
  secondary_count int DEFAULT 2    -- Works ABOUT the author
)
RETURNS TABLE (
  chunk_text text,
  similarity float,
  metadata jsonb,
  source_file text,
  corpus_type text,
  relevance_weight float
) AS $$
DECLARE
  embedding_vector vector(1536);
BEGIN
  embedding_vector := query_embedding::vector;
  
  -- Union primary and secondary results with different weights
  RETURN QUERY
  (
    -- Primary works (works_by) - weighted higher for authentic voice
    SELECT 
      v.chunk_text,
      1 - (v.embedding <=> embedding_vector) as similarity,
      v.metadata,
      v.source_file,
      v.corpus_type,
      1.0::float as relevance_weight  -- Full weight for primary sources
    FROM public.faculty_corpus_vectors v
    WHERE v.faculty_id = faculty_id_param
      AND v.corpus_type = 'works_by'
      AND v.embedding IS NOT NULL
      AND 1 - (v.embedding <=> embedding_vector) >= match_threshold
    ORDER BY v.embedding <=> embedding_vector
    LIMIT primary_count
  )
  UNION ALL
  (
    -- Secondary works (works_about) - weighted lower, for context
    SELECT 
      v.chunk_text,
      1 - (v.embedding <=> embedding_vector) as similarity,
      v.metadata,
      v.source_file,
      v.corpus_type,
      0.6::float as relevance_weight  -- Lower weight for secondary sources
    FROM public.faculty_corpus_vectors v
    WHERE v.faculty_id = faculty_id_param
      AND v.corpus_type = 'works_about'
      AND v.embedding IS NOT NULL
      AND 1 - (v.embedding <=> embedding_vector) >= match_threshold
    ORDER BY v.embedding <=> embedding_vector
    LIMIT secondary_count
  )
  ORDER BY (similarity * relevance_weight) DESC;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION get_faculty_hybrid_rag_context IS 'Get hybrid RAG context combining primary works (speaking AS) and secondary literature (speaking ABOUT). Primary sources weighted higher for authentic voice.';

-- ============================================================================
-- CORPUS STATUS FUNCTION WITH TYPE BREAKDOWN
-- ============================================================================

-- Enhanced status function showing breakdown by corpus type
CREATE OR REPLACE FUNCTION get_faculty_corpus_status(faculty_id_param text)
RETURNS TABLE (
  total_chunks int,
  works_by_chunks int,
  works_about_chunks int,
  source_files text[],
  last_updated timestamptz
) AS $$
BEGIN
  RETURN QUERY
  SELECT 
    COUNT(*)::int as total_chunks,
    COUNT(*) FILTER (WHERE corpus_type = 'works_by')::int as works_by_chunks,
    COUNT(*) FILTER (WHERE corpus_type = 'works_about')::int as works_about_chunks,
    ARRAY_AGG(DISTINCT source_file) FILTER (WHERE source_file IS NOT NULL) as source_files,
    MAX(updated_at) as last_updated
  FROM public.faculty_corpus_vectors
  WHERE faculty_id = faculty_id_param;
END;
$$ LANGUAGE plpgsql;

-- ============================================================================
-- ADD CORPUS_TYPE TO BIBLIOTECH BOOKS TABLE (IF IT EXISTS)
-- ============================================================================

DO $$
BEGIN
  -- Check if books table exists and add corpus_type column
  IF EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'books' AND table_schema = 'public') THEN
    EXECUTE 'ALTER TABLE public.books ADD COLUMN IF NOT EXISTS corpus_type text DEFAULT ''works_by''';
    EXECUTE 'CREATE INDEX IF NOT EXISTS idx_books_corpus_type ON public.books (faculty_id, corpus_type)';
    RAISE NOTICE 'Added corpus_type column to books table';
  END IF;
END
$$;

-- ============================================================================
-- SUMMARY
-- ============================================================================

-- This migration adds:
-- 1. corpus_type column to faculty_corpus_vectors ('works_by', 'works_about', 'general')
-- 2. Updated get_faculty_rag_context() with optional corpus_type filtering
-- 3. New get_faculty_hybrid_rag_context() for blended retrieval
-- 4. Updated get_faculty_corpus_status() with type breakdown
-- 5. corpus_type column to books table (if exists)
--
-- Usage examples:
--   SELECT * FROM get_faculty_rag_context('a.plato', embedding, 0.7, 5, 'works_by');
--   SELECT * FROM get_faculty_rag_context('a.plato', embedding, 0.7, 5, 'works_about');
--   SELECT * FROM get_faculty_hybrid_rag_context('a.plato', embedding, 0.6, 4, 2);
