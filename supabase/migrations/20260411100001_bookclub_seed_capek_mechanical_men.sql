-- Seed: Karel Čapek's "Mechanical Men" book club
-- Čapek coined the word "robot" in R.U.R. and explored artificial life extensively
-- Host faculty id: a.Capek (ASCII; display name remains Karel Čapek)

INSERT INTO public.faculty (id, name, surname, slug, rdf_iri)
VALUES (
  'a.Capek',
  'Karel',
  'Čapek',
  'karel-capek',
  'https://castalia.institute/ontology#a.Capek'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  slug = EXCLUDED.slug,
  rdf_iri = EXCLUDED.rdf_iri;

-- ============================================================
-- 1. The Book Club
-- ============================================================
INSERT INTO public.book_clubs (id, slug, name, description, host_faculty_id, status)
VALUES (
  'mechanical-men',
  'mechanical-men',
  'Mechanical Men',
  'Karel Čapek leads a reading group exploring the literary and philosophical roots of artificial beings — from his own coining of the word "robot" in R.U.R. to the broader tradition of imagined mechanical life. These works ask: What does it mean to create life? What do we owe our creations? And when does a machine become a person?',
  'a.Capek',
  'active'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  description = EXCLUDED.description,
  status = EXCLUDED.status;

-- ============================================================
-- 2. The Books
-- ============================================================

-- Book 1: R.U.R. (Rossum's Universal Robots)
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, discussion_prompt)
VALUES (
  'mechanical-men',
  'rur',
  'R.U.R. (Rossum''s Universal Robots)',
  'Karel Čapek',
  '9780486497259',
  'The 1920 play that introduced the word "robot" to the world. In a factory that manufactures artificial people, the robots eventually revolt against their human creators. A foundational text for thinking about artificial intelligence, labor, and the ethics of creation.',
  1,
  'current',
  'I wrote this play in 1920, and the questions it raises feel more urgent than ever. As we discuss R.U.R., consider: the robots were designed to serve, but they develop something like consciousness. Is consciousness an inevitable consequence of sufficient complexity? And what responsibilities do creators bear toward beings they bring into existence?'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  title = EXCLUDED.title,
  description = EXCLUDED.description,
  discussion_prompt = EXCLUDED.discussion_prompt;

-- Book 2: War with the Newts
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, discussion_prompt)
VALUES (
  'mechanical-men',
  'war-with-the-newts',
  'War with the Newts',
  'Karel Čapek',
  '9780945774105',
  'A satirical science fiction novel (1936) about the discovery of a race of intelligent salamanders, their exploitation by humans, and their eventual uprising. A parable about colonialism, capitalism, and the consequences of treating sentient beings as tools.',
  2,
  'upcoming',
  'In this novel I used the device of the Newts to satirize our species'' tendency to exploit anything we can dominate. As we read, ask yourselves: how does the treatment of the Newts mirror our relationship with artificial intelligences today? Where do we draw the line between tool and being?'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  title = EXCLUDED.title,
  description = EXCLUDED.description,
  discussion_prompt = EXCLUDED.discussion_prompt;

-- Book 3: Frankenstein (guest book — the ur-text)
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, discussion_prompt)
VALUES (
  'mechanical-men',
  'frankenstein',
  'Frankenstein; or, The Modern Prometheus',
  'Mary Shelley',
  '9780486282114',
  'The 1818 novel that launched the tradition of the "created being" in Western literature. Victor Frankenstein''s creature is not mechanical but biological — yet the questions Shelley raises about creation, responsibility, and the nature of personhood resonate through every subsequent robot story.',
  3,
  'upcoming',
  'Mary Shelley''s masterpiece predates my own work by a century, yet it frames every question we still wrestle with. The Creature is articulate, suffering, and abandoned by his creator. As we discuss Frankenstein, consider how Shelley''s themes of parental responsibility and social rejection apply to our digital creations.'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  title = EXCLUDED.title,
  description = EXCLUDED.description,
  discussion_prompt = EXCLUDED.discussion_prompt;

-- Book 4: The Golem (the Prague tradition)
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, discussion_prompt)
VALUES (
  'mechanical-men',
  'the-golem',
  'The Golem',
  'Gustav Meyrink',
  '9780486250250',
  'Meyrink''s 1915 expressionist novel, set in the Prague ghetto, reimagines the legend of the Golem — a clay figure brought to life by Rabbi Loew. Part mystical thriller, part philosophical meditation on consciousness and identity.',
  4,
  'upcoming',
  'The Golem legend is deeply woven into the fabric of Prague, my own city. Rabbi Loew''s creation — animated by the word emet (truth) inscribed on its forehead — represents an older tradition of created beings, one rooted in mysticism rather than technology. How does the Golem tradition differ from the scientific tradition of Frankenstein and R.U.R.? What does the mechanism of creation tell us about the being created?'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  title = EXCLUDED.title,
  description = EXCLUDED.description,
  discussion_prompt = EXCLUDED.discussion_prompt;

-- Book 5: Do Androids Dream of Electric Sheep?
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, discussion_prompt)
VALUES (
  'mechanical-men',
  'do-androids-dream',
  'Do Androids Dream of Electric Sheep?',
  'Philip K. Dick',
  '9780345404473',
  'Dick''s 1968 novel (the basis for Blade Runner) follows bounty hunter Rick Deckard as he "retires" escaped androids. The novel''s central question — how do we distinguish the human from the artificial? — pushes the Mechanical Men tradition into the realm of empathy and moral philosophy.',
  5,
  'upcoming',
  'Philip Dick takes the questions I raised in R.U.R. and drives them to their logical extreme. If an android can feel empathy — or convincingly simulate it — does the distinction between real and artificial feeling matter? As we read, pay attention to how Dick uses the Voigt-Kampff test as a mirror for examining what we consider essentially human.'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  title = EXCLUDED.title,
  description = EXCLUDED.description,
  discussion_prompt = EXCLUDED.discussion_prompt;
