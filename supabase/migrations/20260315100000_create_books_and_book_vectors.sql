-- Create books table and book_vectors table for faculty book agents
-- Books by faculty members can be queried via the ask-book endpoint

-- ============================================================================
-- BOOKS TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.books (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  faculty_id text NOT NULL REFERENCES public.faculty(id) ON DELETE CASCADE,
  title text NOT NULL,
  subtitle text,
  author_name text NOT NULL,
  isbn text,
  publication_year int,
  publisher text,
  description text,
  cover_image_url text,
  content_url text,
  language text DEFAULT 'en',
  page_count int,
  tags text[] DEFAULT '{}',
  agent_persona text,
  status text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'draft', 'archived')),
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_books_faculty_id ON public.books (faculty_id);
CREATE INDEX IF NOT EXISTS idx_books_status ON public.books (status);
CREATE UNIQUE INDEX IF NOT EXISTS idx_books_isbn ON public.books (isbn) WHERE isbn IS NOT NULL;

COMMENT ON TABLE public.books IS 'Books authored by faculty members. Each book can have its own AI agent via ask-book endpoint.';
COMMENT ON COLUMN public.books.faculty_id IS 'Faculty member who authored the book';
COMMENT ON COLUMN public.books.agent_persona IS 'Custom system prompt for this book''s agent. If null, auto-generated from book metadata + faculty persona.';
COMMENT ON COLUMN public.books.content_url IS 'URL to the full text (GCS, S3, etc.) used for corpus ingestion';

-- ============================================================================
-- BOOK_VECTORS TABLE (pgvector RAG)
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.book_vectors (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  book_id uuid NOT NULL REFERENCES public.books(id) ON DELETE CASCADE,
  chunk_text text NOT NULL,
  chunk_index int NOT NULL DEFAULT 0,
  embedding vector(1536),
  metadata jsonb DEFAULT '{}',
  source_file text,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_book_vectors_book_id ON public.book_vectors (book_id);
CREATE INDEX IF NOT EXISTS idx_book_vectors_embedding ON public.book_vectors
  USING ivfflat (embedding vector_cosine_ops) WITH (lists = 50);

COMMENT ON TABLE public.book_vectors IS 'Vector embeddings of book content chunks for semantic search (RAG)';

-- ============================================================================
-- HELPER: check if a book has vectors
-- ============================================================================

CREATE OR REPLACE FUNCTION book_has_vectors(book_id_param uuid)
RETURNS boolean AS $$
BEGIN
  RETURN EXISTS (
    SELECT 1 FROM public.book_vectors
    WHERE book_id = book_id_param AND embedding IS NOT NULL
    LIMIT 1
  );
END;
$$ LANGUAGE plpgsql STABLE;

-- ============================================================================
-- RAG FUNCTION: get_book_rag_context
-- ============================================================================

CREATE OR REPLACE FUNCTION get_book_rag_context(
  book_id_param uuid,
  query_embedding text,
  match_threshold float DEFAULT 0.5,
  match_count int DEFAULT 5
)
RETURNS TABLE (
  chunk_text text,
  similarity float,
  metadata jsonb,
  source_file text,
  chunk_index int
) AS $$
DECLARE
  embedding_vector vector(1536);
BEGIN
  embedding_vector := query_embedding::vector;

  RETURN QUERY
  SELECT
    v.chunk_text,
    1 - (v.embedding <=> embedding_vector) as similarity,
    v.metadata,
    v.source_file,
    v.chunk_index
  FROM public.book_vectors v
  WHERE v.book_id = book_id_param
    AND v.embedding IS NOT NULL
    AND 1 - (v.embedding <=> embedding_vector) >= match_threshold
  ORDER BY v.embedding <=> embedding_vector
  LIMIT match_count;
END;
$$ LANGUAGE plpgsql STABLE;

COMMENT ON FUNCTION get_book_rag_context IS 'Semantic search over book content vectors. Returns the most relevant chunks for a given query embedding.';

-- ============================================================================
-- STATUS FUNCTION
-- ============================================================================

CREATE OR REPLACE FUNCTION get_book_corpus_status(book_id_param uuid)
RETURNS TABLE (
  total_chunks int,
  has_embeddings boolean,
  source_files text[],
  last_updated timestamptz
) AS $$
BEGIN
  RETURN QUERY
  SELECT
    COUNT(*)::int as total_chunks,
    bool_or(v.embedding IS NOT NULL) as has_embeddings,
    ARRAY_AGG(DISTINCT v.source_file) FILTER (WHERE v.source_file IS NOT NULL) as source_files,
    MAX(v.updated_at) as last_updated
  FROM public.book_vectors v
  WHERE v.book_id = book_id_param;
END;
$$ LANGUAGE plpgsql STABLE;

-- ============================================================================
-- RLS POLICIES
-- ============================================================================

ALTER TABLE public.books ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.book_vectors ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Books are viewable by everyone"
  ON public.books FOR SELECT
  USING (status = 'active');

CREATE POLICY "Books are managed by service role"
  ON public.books FOR ALL
  USING (auth.role() = 'service_role');

CREATE POLICY "Book vectors are viewable by everyone"
  ON public.book_vectors FOR SELECT
  USING (true);

CREATE POLICY "Book vectors are managed by service role"
  ON public.book_vectors FOR ALL
  USING (auth.role() = 'service_role');

-- ============================================================================
-- UPDATED_AT TRIGGER
-- ============================================================================

CREATE OR REPLACE FUNCTION update_books_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER books_updated_at
  BEFORE UPDATE ON public.books
  FOR EACH ROW EXECUTE FUNCTION update_books_updated_at();

CREATE TRIGGER book_vectors_updated_at
  BEFORE UPDATE ON public.book_vectors
  FOR EACH ROW EXECUTE FUNCTION update_books_updated_at();
