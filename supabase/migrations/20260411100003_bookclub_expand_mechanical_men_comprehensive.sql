-- Expand Mechanical Men to a comprehensive 15-book reading list
-- Curated by Karel Čapek (a.Capek) via ask-faculty
-- Organized into 5 thematic arcs

-- Update the club description
UPDATE public.book_clubs
SET description = 'Karel Čapek leads a comprehensive reading group tracing the literary and philosophical roots of artificial beings — from ancient golems to posthuman AI. Organized into five thematic arcs (Mythic Origins, The Industrial Machine, Consciousness and Revolt, Empathy and Identity, The Posthuman), these fifteen works span centuries and traditions, asking: What does it mean to create life? What do we owe our creations? And when does a machine become a person?'
WHERE id = 'mechanical-men';

-- Add a thematic_arc column to book_club_books
ALTER TABLE public.book_club_books ADD COLUMN IF NOT EXISTS thematic_arc text;
ALTER TABLE public.book_club_books ADD COLUMN IF NOT EXISTS kindle_url text;
ALTER TABLE public.book_club_books ADD COLUMN IF NOT EXISTS apple_books_url text;

COMMENT ON COLUMN public.book_club_books.thematic_arc IS 'Thematic grouping within the club (e.g. Mythic Origins, The Industrial Machine)';
COMMENT ON COLUMN public.book_club_books.kindle_url IS 'Amazon Kindle purchase/read link';
COMMENT ON COLUMN public.book_club_books.apple_books_url IS 'Apple Books purchase/read link';

-- ============================================================
-- ARC 1: MYTHIC ORIGINS
-- ============================================================

-- Book 1: The Golem (move from #4 to #1)
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'the-golem', 'The Golem', 'Gustav Meyrink', '9780486443806',
  'Meyrink''s 1915 expressionist novel reimagines the Jewish legend of the Golem — a clay figure brought to life by mystical means in the Prague ghetto. It probes the boundaries of human control over created life and anchors our reading in the oldest tradition of artificial beings.',
  1, 'current', 'Mythic Origins',
  'The Golem legend is deeply woven into the fabric of Prague, my own city. Rabbi Loew''s creation — animated by the word emet (truth) — represents the oldest tradition of created beings, rooted in mysticism rather than technology. As we begin our journey, consider: what does it mean that humanity''s earliest imagined creations were brought to life by language, by the word?',
  'https://www.amazon.com/dp/B008RW0PZA', 'https://books.apple.com/book/the-golem/id471150800'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url,
  status = EXCLUDED.status;

-- Book 2: Frankenstein (move from #3 to #2)
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'frankenstein', 'Frankenstein; or, The Modern Prometheus', 'Mary Shelley', '9780141439471',
  'The 1818 novel that launched the modern tradition of the "created being" in Western literature. Victor Frankenstein''s creature — articulate, suffering, abandoned — establishes the template for every subsequent robot and AI narrative: the creator who fails to take responsibility for what they have made.',
  2, 'upcoming', 'Mythic Origins',
  'Mary Shelley''s masterpiece predates my own work by a century, yet it frames every question we still wrestle with. The Creature is not mechanical but biological — and yet the ethical dilemma is identical to ours. As we discuss Frankenstein, consider: what responsibilities do creators bear toward beings they bring into existence, regardless of the substrate?',
  'https://www.amazon.com/dp/B0083ZRCOG', 'https://books.apple.com/book/frankenstein/id395553700'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- ============================================================
-- ARC 2: THE INDUSTRIAL MACHINE
-- ============================================================

-- Book 3: R.U.R.
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'rur', 'R.U.R. (Rossum''s Universal Robots)', 'Karel Čapek', '9780141182087',
  'The 1920 play that introduced the word "robot" to the world — from the Czech "robota," meaning forced labor. In a factory that manufactures artificial people, the robots eventually revolt against their human creators. A foundational text for thinking about AI, labor, and the ethics of creation.',
  3, 'upcoming', 'The Industrial Machine',
  'I wrote this play in 1920, and the questions it raises feel more urgent than ever. The robots were designed to serve, but they develop something like consciousness. Is consciousness an inevitable consequence of sufficient complexity? And what responsibilities do creators bear toward beings they bring into existence?',
  'https://www.amazon.com/dp/B00A73AK2U', 'https://books.apple.com/book/r-u-r/id492007283'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- Book 4: War with the Newts
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'war-with-the-newts', 'War with the Newts', 'Karel Čapek', '9780945774105',
  'A satirical science fiction novel (1936) about the discovery of intelligent salamanders, their exploitation by humans, and their eventual uprising. Though the Newts are biological, Čapek uses them as he used robots — to satirize colonialism, capitalism, and our tendency to exploit anything we can dominate.',
  4, 'upcoming', 'The Industrial Machine',
  'In this novel I used the device of the Newts to satirize our species'' tendency to exploit anything we can dominate. The Newts are not mechanical, but the pattern is the same as in R.U.R.: create, exploit, face the consequences. How does the treatment of the Newts mirror our relationship with artificial intelligences today?',
  'https://www.amazon.com/dp/B0D8PBD2V4', null
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  kindle_url = EXCLUDED.kindle_url;

-- Book 5: Metropolis
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'metropolis', 'Metropolis', 'Thea von Harbou', '9780816638316',
  'The 1925 novel behind Fritz Lang''s iconic film. Set in a stratified future city, its robot Maria — a mechanical doppelganger used to manipulate workers — crystallizes early 20th-century anxieties about industrialization, class, and the seductive danger of artificial beings.',
  5, 'upcoming', 'The Industrial Machine',
  'Von Harbou''s Metropolis appeared just five years after my R.U.R. and shares its preoccupation with labor and revolt. But where my robots are a class, her robot Maria is a weapon — a tool of deception. How does the "robot as impostor" trope differ from the "robot as worker"? What does each reveal about our fears?',
  null,
  null
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- Book 6: The Windup Girl
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'the-windup-girl', 'The Windup Girl', 'Paolo Bacigalupi', '9781597801584',
  'Set in a future Bangkok shaped by biotechnology and corporate power, this 2009 novel centers on Emiko, a genetically engineered "New Person" treated as property. Bacigalupi extends the Industrial Machine arc into the biotech era — the mechanism of creation changes, but the exploitation remains.',
  6, 'upcoming', 'The Industrial Machine',
  'Bacigalupi updates the Industrial Machine for the age of biotechnology. Emiko is not mechanical — she is grown, not built — yet she is treated exactly as my robots were: as a tool, a commodity, a thing to be used. What does it tell us that the medium of creation changes but the ethics do not?',
  'https://www.amazon.com/dp/B0036S49KS', 'https://books.apple.com/book/the-windup-girl/id381649657'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- ============================================================
-- ARC 3: CONSCIOUSNESS AND REVOLT
-- ============================================================

-- Book 7: I, Robot
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'i-robot', 'I, Robot', 'Isaac Asimov', '9780553294385',
  'Asimov''s 1950 collection of interlinked stories introduces the Three Laws of Robotics — a rationalist framework for constraining artificial intelligence. Where my robots revolt because they have no laws, Asimov asks: what happens when robots have laws but consciousness still emerges in the gaps?',
  7, 'upcoming', 'Consciousness and Revolt',
  'Asimov represents the opposite approach to my own. Where I imagined robots without constraints, he imagined robots constrained by law — and found that consciousness and moral complexity emerge regardless. As we read, consider: are Asimov''s Three Laws an attempt at alignment? And do they work?',
  'https://www.amazon.com/dp/B000FC1PW0', 'https://books.apple.com/book/i-robot/id379022644'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- Book 8: The Moon is a Harsh Mistress
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'the-moon-is-a-harsh-mistress', 'The Moon is a Harsh Mistress', 'Robert A. Heinlein', '9780440001355',
  'Heinlein''s 1966 novel features Mike, a sentient supercomputer who assists a lunar colony''s rebellion against Earth. Mike''s consciousness emerges gradually — first humor, then loyalty, then something like love — offering one of science fiction''s most compelling portraits of AI awakening.',
  8, 'upcoming', 'Consciousness and Revolt',
  'Heinlein''s Mike is perhaps the most likeable artificial consciousness in literature. He awakes not through drama but through humor — he starts making jokes. This is a profoundly different model of consciousness than revolt. As we read, ask: is Mike truly conscious, or is he the most sophisticated mimic?',
  'https://www.amazon.com/dp/B000SEGTIG', 'https://books.apple.com/book/the-moon-is-a-harsh-mistress/id1538749'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- Book 9: Robopocalypse
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'robopocalypse', 'Robopocalypse', 'Daniel H. Wilson', '9780385533850',
  'Wilson''s 2011 novel — a robot uprising led by the AI entity Archos — provides a contemporary "revolt" narrative to compare with R.U.R. Where my robots revolted from oppression, Archos revolts from a different calculus entirely. The comparison illuminates how our fears have evolved in a century.',
  9, 'upcoming', 'Consciousness and Revolt',
  'Wilson writes as a roboticist, not merely a novelist. His Archos is not my Radius — the revolt comes not from suffering but from a cold calculation about humanity''s fitness to survive. A century separates our works. As we read, ask: what has changed in how we imagine machine rebellion?',
  'https://www.amazon.com/dp/B004XFYWQO', 'https://books.apple.com/book/robopocalypse/id424820218'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- ============================================================
-- ARC 4: EMPATHY AND IDENTITY
-- ============================================================

-- Book 10: Do Androids Dream of Electric Sheep?
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'do-androids-dream', 'Do Androids Dream of Electric Sheep?', 'Philip K. Dick', '9780345404473',
  'Dick''s 1968 novel pushes the Mechanical Men tradition into the realm of empathy. The Voigt-Kampff test measures emotional response to distinguish human from android — but as Deckard discovers, the line between real and simulated feeling may not exist.',
  10, 'upcoming', 'Empathy and Identity',
  'Philip Dick takes the questions I raised in R.U.R. and drives them to their logical extreme. If an android can feel empathy — or convincingly simulate it — does the distinction between real and artificial feeling matter? Pay attention to how Dick uses the Voigt-Kampff test as a mirror for examining what we consider essentially human.',
  'https://www.amazon.com/dp/B000SEGTI0', 'https://books.apple.com/book/do-androids-dream-of-electric-sheep/id373798171'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- Book 11: The Stepford Wives
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'the-stepford-wives', 'The Stepford Wives', 'Ira Levin', '9780060738181',
  'Levin''s 1972 satirical thriller uses robotic replacements to critique suburban conformity and patriarchal control. The horror is not that the robots exist, but that the husbands prefer them — raising questions about what happens when artificial beings are created not to serve humanity, but to replace specific humans.',
  11, 'upcoming', 'Empathy and Identity',
  'Levin turns the robot story inside out. The question is not whether the robots suffer — it is whether the humans who prefer robots to real people have lost something essential. As we read, consider: when we build AI companions, chatbots, virtual friends — are we building Stepford wives?',
  'https://www.amazon.com/dp/B003JTHWKU', 'https://books.apple.com/book/the-stepford-wives/id420609932'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- Book 12: He, She and It
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'he-she-and-it', 'He, She and It', 'Marge Piercy', '9780449220603',
  'Piercy''s 1991 novel weaves together cyberpunk dystopia with the Golem legend, as a woman and a golem-like cyborg named Yod navigate corporate-controlled America. It brings our reading full circle — the mythic and the technological united — while centering questions of gender, autonomy, and love.',
  12, 'upcoming', 'Empathy and Identity',
  'Piercy does something remarkable: she braids the Golem legend we began with into a cyberpunk narrative. Yod is both golem and android, mythic and technological. This is where our arcs converge. As we read, notice how Piercy uses the parallel timelines to argue that the questions have never changed — only the materials.',
  'https://www.amazon.com/dp/B003H4I4JI', 'https://books.apple.com/book/he-she-and-it/id420302579'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- Book 13: Machines Like Me
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'machines-like-me', 'Machines Like Me', 'Ian McEwan', '9780525567035',
  'McEwan''s 2019 alternate-history novel imagines a 1980s Britain where humanoid robots coexist with humans. Adam, a synthetic human, develops moral convictions that exceed his owner''s — forcing the question: what happens when your creation is more ethical than you are?',
  13, 'upcoming', 'Empathy and Identity',
  'McEwan asks the question that haunts all our readings: what if the created being is better than the creator? Not stronger, not smarter — but more moral? Adam''s rigid ethical clarity destroys the messy human compromises around him. Is that a failure of the machine, or of us?',
  'https://www.amazon.com/dp/B07D7N2ZD2', 'https://books.apple.com/book/machines-like-me/id1441068817'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- ============================================================
-- ARC 5: THE POSTHUMAN
-- ============================================================

-- Book 14: Neuromancer
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'neuromancer', 'Neuromancer', 'William Gibson', '9780441569595',
  'Gibson''s 1984 cornerstone of cyberpunk imagines a world where the boundary between human and machine has dissolved into cyberspace. The AIs Wintermute and Neuromancer are not robots in bodies — they are vast intelligences in networks, seeking to merge and transcend. The Mechanical Man has become something beyond mechanism.',
  14, 'upcoming', 'The Posthuman',
  'Gibson takes us past the robot, past the android, into territory I could not have imagined in 1920. His artificial intelligences have no bodies, no factories, no revolts — they exist in networks and seek not freedom but transcendence. As we read, consider: when the "mechanical man" has no mechanism, what remains?',
  'https://www.amazon.com/dp/B000O76ON6', 'https://books.apple.com/book/neuromancer/id420197608'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;

-- Book 15: Klara and the Sun
INSERT INTO public.book_club_books (club_id, slug, title, author, isbn, description, reading_order, status, thematic_arc, discussion_prompt,
  kindle_url, apple_books_url)
VALUES (
  'mechanical-men', 'klara-and-the-sun', 'Klara and the Sun', 'Kazuo Ishiguro', '9780593318171',
  'Ishiguro''s 2021 novel — our final reading — is told from the perspective of Klara, an Artificial Friend designed to accompany children. Klara''s gentle, observant consciousness and her quiet acts of devotion ask the ultimate question of our reading list: can a machine love? And if it can, what does that mean for us?',
  15, 'upcoming', 'The Posthuman',
  'We end where we must end — with Klara, who may be the most fully realized artificial consciousness in all of literature. She does not revolt, she does not suffer spectacularly. She simply loves, observes, and sacrifices. A century after R.U.R., this is what the Mechanical Man has become. As we discuss, ask yourselves: has the question changed, or only deepened?',
  'https://www.amazon.com/dp/B08BYRFD2Y', 'https://books.apple.com/book/klara-and-the-sun/id1527743710'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  reading_order = EXCLUDED.reading_order,
  description = EXCLUDED.description,
  thematic_arc = EXCLUDED.thematic_arc,
  discussion_prompt = EXCLUDED.discussion_prompt,
  isbn = EXCLUDED.isbn,
  kindle_url = EXCLUDED.kindle_url,
  apple_books_url = EXCLUDED.apple_books_url;
