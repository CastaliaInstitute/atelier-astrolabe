-- Semantic search (RAG) over curated faculty quotes by topic.

CREATE EXTENSION IF NOT EXISTS vector;

ALTER TABLE public.faculty_quotes
  ADD COLUMN IF NOT EXISTS search_text text,
  ADD COLUMN IF NOT EXISTS embedding vector(1536),
  ADD COLUMN IF NOT EXISTS embedded_at timestamptz;

COMMENT ON COLUMN public.faculty_quotes.search_text IS
  'Document embedded for topic search (quote, passage, work, tags).';
COMMENT ON COLUMN public.faculty_quotes.embedding IS
  'OpenAI text-embedding-3-small (1536) for semantic topic search.';

CREATE OR REPLACE FUNCTION public.sync_faculty_quotes_search_text()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  NEW.search_text := trim(concat_ws(
    E'\n',
    NEW.quote_text,
    NULLIF(btrim(NEW.passage_label), ''),
    CASE
      WHEN NEW.book_title IS NOT NULL AND btrim(NEW.book_title) <> '' THEN
        NEW.book_title || COALESCE(' by ' || NULLIF(btrim(NEW.book_author), ''), '')
    END,
    CASE
      WHEN NEW.tags IS NOT NULL AND cardinality(NEW.tags) > 0 THEN
        'Topics: ' || array_to_string(NEW.tags, ', ')
    END
  ));

  IF TG_OP = 'UPDATE'
     AND OLD.search_text IS DISTINCT FROM NEW.search_text THEN
    NEW.embedding := NULL;
    NEW.embedded_at := NULL;
  END IF;

  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS faculty_quotes_sync_search_text ON public.faculty_quotes;
CREATE TRIGGER faculty_quotes_sync_search_text
  BEFORE INSERT OR UPDATE ON public.faculty_quotes
  FOR EACH ROW
  EXECUTE FUNCTION public.sync_faculty_quotes_search_text();

-- Backfill search_text for existing rows
UPDATE public.faculty_quotes q
SET search_text = trim(concat_ws(
  E'\n',
  q.quote_text,
  NULLIF(btrim(q.passage_label), ''),
  CASE
    WHEN q.book_title IS NOT NULL AND btrim(q.book_title) <> '' THEN
      q.book_title || COALESCE(' by ' || NULLIF(btrim(q.book_author), ''), '')
  END,
  CASE
    WHEN q.tags IS NOT NULL AND cardinality(q.tags) > 0 THEN
      'Topics: ' || array_to_string(q.tags, ', ')
  END
))
WHERE q.search_text IS NULL;

DO $$
BEGIN
  IF EXISTS (
    SELECT 1 FROM public.faculty_quotes
    WHERE embedding IS NOT NULL
    LIMIT 1
  ) THEN
    CREATE INDEX IF NOT EXISTS idx_faculty_quotes_embedding
      ON public.faculty_quotes
      USING ivfflat (embedding vector_cosine_ops)
      WITH (lists = 16)
      WHERE embedding IS NOT NULL AND status = 'published';
  END IF;
EXCEPTION
  WHEN OTHERS THEN
    RAISE NOTICE 'faculty_quotes vector index deferred: %', SQLERRM;
END $$;

CREATE OR REPLACE FUNCTION public.search_faculty_quotes(
  query_embedding vector(1536),
  match_count int DEFAULT 10,
  match_threshold float DEFAULT 0.35,
  filter_faculty_id text DEFAULT NULL
)
RETURNS TABLE (
  id uuid,
  faculty_id text,
  faculty_name text,
  quote_text text,
  passage_label text,
  book_title text,
  book_author text,
  tags text[],
  similarity float,
  book_id uuid,
  readest_url text
)
LANGUAGE sql
STABLE
AS $$
  SELECT
    q.id,
    q.faculty_id,
    f.name AS faculty_name,
    q.quote_text,
    q.passage_label,
    q.book_title,
    q.book_author,
    q.tags,
    (1 - (q.embedding <=> query_embedding))::float AS similarity,
    q.book_id,
    public.faculty_quote_readest_url(q.book_id, q.cfi) AS readest_url
  FROM public.faculty_quotes q
  JOIN public.faculty f ON f.id = q.faculty_id
  WHERE q.status = 'published'
    AND q.embedding IS NOT NULL
    AND (filter_faculty_id IS NULL OR q.faculty_id = filter_faculty_id)
    AND (1 - (q.embedding <=> query_embedding)) >= match_threshold
  ORDER BY q.embedding <=> query_embedding
  LIMIT match_count;
$$;

COMMENT ON FUNCTION public.search_faculty_quotes IS
  'Topic search over published quotes. Pass a 1536-dim query embedding (e.g. from OpenAI text-embedding-3-small).';

GRANT EXECUTE ON FUNCTION public.search_faculty_quotes(vector, int, float, text) TO anon, authenticated, service_role;
