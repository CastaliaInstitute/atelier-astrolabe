-- Book Clubs: topical reading groups hosted by faculty
-- Each club is a Matrix space, each book gets its own room

-- ============================================================
-- 1. book_clubs — top-level clubs (one per topic / faculty host)
-- ============================================================
CREATE TABLE IF NOT EXISTS public.book_clubs (
  id            text        PRIMARY KEY,
  slug          text        UNIQUE NOT NULL,
  name          text        NOT NULL,
  description   text        NOT NULL DEFAULT '',
  host_faculty_id text      NOT NULL REFERENCES public.faculty(id),
  cover_image_url text,
  matrix_space_id   text,       -- Matrix space ID (!xxx:matrix.castalia.institute)
  matrix_space_alias text,      -- #bookclub-{slug}:matrix.castalia.institute
  status        text        NOT NULL DEFAULT 'upcoming'
                            CHECK (status IN ('active', 'upcoming', 'archived')),
  created_at    timestamptz NOT NULL DEFAULT now(),
  updated_at    timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX idx_book_clubs_slug ON public.book_clubs(slug);
CREATE INDEX idx_book_clubs_host ON public.book_clubs(host_faculty_id);
CREATE INDEX idx_book_clubs_status ON public.book_clubs(status);

-- ============================================================
-- 2. book_club_books — individual books within a club
-- ============================================================
CREATE TABLE IF NOT EXISTS public.book_club_books (
  id              uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
  club_id         text        NOT NULL REFERENCES public.book_clubs(id) ON DELETE CASCADE,
  slug            text        NOT NULL,
  title           text        NOT NULL,
  author          text        NOT NULL,
  isbn            text,
  readest_url     text,       -- direct link to Readest reader
  bibliotech_url  text,       -- link to Bibliotech catalog entry
  cover_image_url text,
  description     text,
  reading_order   integer     NOT NULL DEFAULT 0,
  matrix_room_id    text,     -- room for this book's discussion
  matrix_room_alias text,     -- #bookclub-{club}-{book}:matrix.castalia.institute
  status          text        NOT NULL DEFAULT 'upcoming'
                              CHECK (status IN ('current', 'upcoming', 'completed')),
  discussion_prompt text,     -- host's framing prompt for the book
  created_at      timestamptz NOT NULL DEFAULT now(),
  updated_at      timestamptz NOT NULL DEFAULT now(),
  UNIQUE(club_id, slug)
);

CREATE INDEX idx_bcb_club ON public.book_club_books(club_id);
CREATE INDEX idx_bcb_status ON public.book_club_books(status);

-- ============================================================
-- 3. book_club_sessions — scheduled reading/discussion sessions
-- ============================================================
CREATE TABLE IF NOT EXISTS public.book_club_sessions (
  id                uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
  book_id           uuid        NOT NULL REFERENCES public.book_club_books(id) ON DELETE CASCADE,
  title             text        NOT NULL,
  description       text,
  scheduled_at      timestamptz,
  duration_minutes  integer     DEFAULT 60,
  chapter_range     text,       -- e.g. "Chapters 1-3"
  discussion_questions text[],
  matrix_event_id   text,       -- scheduled Matrix event
  status            text        NOT NULL DEFAULT 'scheduled'
                                CHECK (status IN ('scheduled', 'in-progress', 'completed')),
  created_at        timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX idx_bcs_book ON public.book_club_sessions(book_id);
CREATE INDEX idx_bcs_scheduled ON public.book_club_sessions(scheduled_at);

-- ============================================================
-- 4. book_club_members — club membership
-- ============================================================
CREATE TABLE IF NOT EXISTS public.book_club_members (
  id              uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
  club_id         text        NOT NULL REFERENCES public.book_clubs(id) ON DELETE CASCADE,
  user_id         uuid,       -- Supabase auth user (nullable for Matrix-only members)
  matrix_user_id  text,       -- @user:matrix.castalia.institute
  display_name    text        NOT NULL,
  role            text        NOT NULL DEFAULT 'member'
                              CHECK (role IN ('host', 'moderator', 'member')),
  joined_at       timestamptz NOT NULL DEFAULT now(),
  UNIQUE(club_id, matrix_user_id)
);

CREATE INDEX idx_bcm_club ON public.book_club_members(club_id);

-- ============================================================
-- 5. RLS Policies
-- ============================================================

ALTER TABLE public.book_clubs ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.book_club_books ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.book_club_sessions ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.book_club_members ENABLE ROW LEVEL SECURITY;

-- Public read for all book club data
CREATE POLICY "Public read book_clubs"
  ON public.book_clubs FOR SELECT
  USING (true);

CREATE POLICY "Public read book_club_books"
  ON public.book_club_books FOR SELECT
  USING (true);

CREATE POLICY "Public read book_club_sessions"
  ON public.book_club_sessions FOR SELECT
  USING (true);

CREATE POLICY "Public read book_club_members"
  ON public.book_club_members FOR SELECT
  USING (true);

-- Service role can manage all data
CREATE POLICY "Service manage book_clubs"
  ON public.book_clubs FOR ALL
  USING (auth.role() = 'service_role');

CREATE POLICY "Service manage book_club_books"
  ON public.book_club_books FOR ALL
  USING (auth.role() = 'service_role');

CREATE POLICY "Service manage book_club_sessions"
  ON public.book_club_sessions FOR ALL
  USING (auth.role() = 'service_role');

CREATE POLICY "Service manage book_club_members"
  ON public.book_club_members FOR ALL
  USING (auth.role() = 'service_role');

-- ============================================================
-- 6. Updated-at trigger
-- ============================================================
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ language 'plpgsql';

CREATE TRIGGER update_book_clubs_updated_at
  BEFORE UPDATE ON public.book_clubs
  FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_book_club_books_updated_at
  BEFORE UPDATE ON public.book_club_books
  FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- ============================================================
-- 7. Comments
-- ============================================================
COMMENT ON TABLE public.book_clubs IS 'Topical book clubs hosted by faculty members';
COMMENT ON TABLE public.book_club_books IS 'Individual books within a book club, each with a Matrix discussion room';
COMMENT ON TABLE public.book_club_sessions IS 'Scheduled discussion sessions for a book';
COMMENT ON TABLE public.book_club_members IS 'Members of a book club';
COMMENT ON COLUMN public.book_clubs.matrix_space_id IS 'Matrix space grouping all book rooms in this club';
COMMENT ON COLUMN public.book_club_books.readest_url IS 'Direct link to read the book in Readest (Bibliotech reader)';
COMMENT ON COLUMN public.book_club_books.discussion_prompt IS 'Host faculty framing prompt used in the Matrix room system prompt';
