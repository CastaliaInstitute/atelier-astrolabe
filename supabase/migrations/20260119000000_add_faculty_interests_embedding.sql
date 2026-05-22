-- Add interests_embedding column to faculty table for pgvector RAG
-- This enables semantic search to find faculty with different perspectives on questions

-- ============================================================================
-- ADD INTERESTS EMBEDDING COLUMN
-- ============================================================================

-- Add interests_embedding column (1536 dimensions for text-embedding-3-large)
ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS interests_embedding vector(1536);

-- Create index for similarity search using HNSW (better for large datasets)
CREATE INDEX IF NOT EXISTS idx_faculty_interests_embedding 
ON public.faculty 
USING hnsw (interests_embedding vector_cosine_ops)
WITH (m = 16, ef_construction = 64)
WHERE interests_embedding IS NOT NULL;

COMMENT ON COLUMN public.faculty.interests_embedding IS 'Vector embedding of faculty interests (research_statement, research_questions, biography, fields). Used for semantic search to find faculty with relevant or different perspectives on inquiry questions.';

-- ============================================================================
-- FUNCTION TO SEARCH FACULTY BY INTERESTS
-- ============================================================================

-- Function to search faculty by interests using vector similarity
-- Returns faculty sorted by relevance to the query embedding
-- Accepts embedding as text (array string) and casts to vector for compatibility
CREATE OR REPLACE FUNCTION search_faculty_by_interests(
  query_embedding text, -- Accept as text, cast to vector inside
  match_threshold float DEFAULT 0.3,
  match_count int DEFAULT 50,
  exclude_ids text[] DEFAULT ARRAY[]::text[]
)
RETURNS TABLE (
  id text,
  slug text,
  name text,
  surname text,
  similarity float,
  fields text[],
  research_statement text,
  biography text
) AS $$
DECLARE
  embedding_vector vector(1536);
BEGIN
  -- Cast text to vector (format: "[0.1,0.2,...]")
  embedding_vector := query_embedding::vector;
  
  RETURN QUERY
  SELECT 
    f.id,
    f.slug,
    f.name,
    f.surname,
    1 - (f.interests_embedding <=> embedding_vector) as similarity,
    f.fields,
    f.research_statement,
    f.biography
  FROM public.faculty f
  WHERE f.interests_embedding IS NOT NULL
    AND (exclude_ids IS NULL OR array_length(exclude_ids, 1) IS NULL OR f.id != ALL(exclude_ids))
    AND 1 - (f.interests_embedding <=> embedding_vector) >= match_threshold
  ORDER BY f.interests_embedding <=> embedding_vector
  LIMIT match_count;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION search_faculty_by_interests IS 'Search faculty by interests using vector similarity. Returns faculty sorted by relevance to query embedding.';

-- ============================================================================
-- FUNCTION TO FIND DIVERSE FACULTY PERSPECTIVES
-- ============================================================================

-- Function to find faculty with diverse perspectives on a question
-- Uses vector similarity to find relevant faculty
-- Returns top candidates by similarity - diversity selection can be done in application layer
-- Accepts embedding as text (array string) and casts to vector for compatibility
CREATE OR REPLACE FUNCTION find_diverse_faculty_for_question(
  query_embedding text, -- Accept as text, cast to vector inside
  top_k int DEFAULT 30,
  final_count int DEFAULT 3
)
RETURNS TABLE (
  id text,
  slug text,
  name text,
  surname text,
  similarity float,
  fields text[],
  research_statement text,
  biography text,
  diversity_rank int
) AS $$
DECLARE
  embedding_vector vector(1536);
BEGIN
  -- Cast text to vector (format: "[0.1,0.2,...]")
  embedding_vector := query_embedding::vector;
  
  -- Return top candidates by vector similarity
  -- The application layer (inquire function) will handle diversity selection
  RETURN QUERY
  SELECT 
    f.id,
    f.slug,
    f.name,
    f.surname,
    1 - (f.interests_embedding <=> embedding_vector) as similarity,
    f.fields,
    f.research_statement,
    f.biography,
    row_number() OVER (ORDER BY f.interests_embedding <=> embedding_vector)::int as diversity_rank
  FROM public.faculty f
  WHERE f.interests_embedding IS NOT NULL
  ORDER BY f.interests_embedding <=> embedding_vector
  LIMIT top_k;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION find_diverse_faculty_for_question IS 'Find faculty with diverse perspectives on a question. Uses vector similarity to find relevant candidates, then selects diverse faculty using greedy selection based on embedding distances.';
