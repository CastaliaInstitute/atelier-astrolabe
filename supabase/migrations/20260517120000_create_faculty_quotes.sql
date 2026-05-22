-- Curated quotations by Castalia faculty authors, linked to Bibliotech books and EPUB locators (CFI).

CREATE TABLE IF NOT EXISTS public.faculty_quotes (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  faculty_id text NOT NULL REFERENCES public.faculty (id) ON DELETE CASCADE,
  book_id uuid REFERENCES public.books (id) ON DELETE SET NULL,
  quote_text text NOT NULL,
  passage_label text,
  cfi text,
  epub_locator jsonb NOT NULL DEFAULT '{}'::jsonb,
  book_title text,
  book_author text,
  source text,
  source_id text,
  tags text[] NOT NULL DEFAULT '{}',
  status text NOT NULL DEFAULT 'published'
    CHECK (status IN ('draft', 'published', 'archived')),
  notes text,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_faculty_quotes_faculty_id
  ON public.faculty_quotes (faculty_id);

CREATE INDEX IF NOT EXISTS idx_faculty_quotes_book_id
  ON public.faculty_quotes (book_id)
  WHERE book_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_faculty_quotes_status
  ON public.faculty_quotes (status)
  WHERE status = 'published';

CREATE UNIQUE INDEX IF NOT EXISTS idx_faculty_quotes_dedup
  ON public.faculty_quotes (faculty_id, md5(quote_text));

COMMENT ON TABLE public.faculty_quotes IS
  'Curated quotations attributed to faculty authors, with Bibliotech book linkage and EPUB CFI for Readest deep reading.';
COMMENT ON COLUMN public.faculty_quotes.book_id IS
  'Bibliotech books.id when the EPUB is in corpus; nullable until linked.';
COMMENT ON COLUMN public.faculty_quotes.cfi IS
  'EPUB Canonical Fragment Identifier for Readest navigation/highlight.';
COMMENT ON COLUMN public.faculty_quotes.epub_locator IS
  'Structured fallback locator (chapter, spine, text anchor) when CFI is not yet resolved.';
COMMENT ON COLUMN public.faculty_quotes.source IS
  'Bibliotech books.source (e.g. gutenberg) for idempotent upsert before book_id is known.';
COMMENT ON COLUMN public.faculty_quotes.source_id IS
  'Bibliotech books.source_id (e.g. Gutenberg ebook id).';

CREATE OR REPLACE FUNCTION public.faculty_quote_readest_url(
  p_book_id uuid,
  p_cfi text DEFAULT NULL
)
RETURNS text
LANGUAGE sql
STABLE
AS $$
  SELECT CASE
    WHEN p_book_id IS NULL THEN NULL
    ELSE 'https://bibliotech.castalia.institute/read/?book=' || p_book_id::text
      || CASE
        WHEN p_cfi IS NOT NULL AND btrim(p_cfi) <> '' THEN '&cfi=' || replace(replace(p_cfi, '&', '%26'), '#', '%23')
        ELSE ''
      END
  END;
$$;

COMMENT ON FUNCTION public.faculty_quote_readest_url IS
  'Bibliotech Readest URL; cfi query param reserved for passage deep-linking.';

CREATE OR REPLACE VIEW public.faculty_quotes_public AS
SELECT
  q.id,
  q.faculty_id,
  f.name AS faculty_name,
  q.book_id,
  q.quote_text,
  q.passage_label,
  q.cfi,
  q.epub_locator,
  q.book_title,
  q.book_author,
  q.source,
  q.source_id,
  q.tags,
  public.faculty_quote_readest_url(q.book_id, q.cfi) AS readest_url,
  q.created_at,
  q.updated_at
FROM public.faculty_quotes q
JOIN public.faculty f ON f.id = q.faculty_id
WHERE q.status = 'published';

COMMENT ON VIEW public.faculty_quotes_public IS
  'Published quotes with faculty display name and computed Readest URL.';

ALTER TABLE public.faculty_quotes ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS faculty_quotes_select_published ON public.faculty_quotes;
CREATE POLICY faculty_quotes_select_published
  ON public.faculty_quotes
  FOR SELECT
  USING (status = 'published');

DROP TRIGGER IF EXISTS update_faculty_quotes_updated_at ON public.faculty_quotes;
CREATE TRIGGER update_faculty_quotes_updated_at
  BEFORE UPDATE ON public.faculty_quotes
  FOR EACH ROW
  EXECUTE FUNCTION update_updated_at_column();
