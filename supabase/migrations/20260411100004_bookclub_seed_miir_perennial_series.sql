-- Machine Intelligence & the Imaginal Realm (Luke Lafitte)
-- The Perennial Philosophy — source texts behind Huxley's anthology (Aldous Huxley)

INSERT INTO public.faculty (id, name, surname, slug, rdf_iri)
VALUES
  (
    'a.lukelafitte',
    'Luke',
    'Lafitte',
    'luke-lafitte',
    'https://castalia.institute/ontology#a.lukelafitte'
  ),
  (
    'a.AldousHuxley',
    'Aldous',
    'Huxley',
    'aldous-huxley',
    'https://castalia.institute/ontology#a.AldousHuxley'
  )
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  slug = EXCLUDED.slug,
  rdf_iri = EXCLUDED.rdf_iri;

-- ============================================================
-- 1. Machine Intelligence and the Imaginal Realm
-- ============================================================
INSERT INTO public.book_clubs (id, slug, name, description, host_faculty_id, status)
VALUES (
  'machine-intelligence-imaginal',
  'machine-intelligence-imaginal',
  'Machine Intelligence and the Imaginal Realm',
  'A topical series on machine intelligence led by Luke Lafitte, centered on his work on the imaginal, spiritual freedom, and the re-animation of matter — the territory where technical and contemplative lines of thought meet.',
  'a.lukelafitte',
  'active'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  description = EXCLUDED.description,
  status = EXCLUDED.status,
  host_faculty_id = EXCLUDED.host_faculty_id;

INSERT INTO public.book_club_books (
  club_id, slug, title, author, isbn, description, reading_order, status,
  discussion_prompt, thematic_arc
)
VALUES (
  'machine-intelligence-imaginal',
  'machine-intelligence-imaginal-realm',
  'Machine Intelligence and the Imaginal Realm: Spiritual Freedom and the Re-animation of Matter',
  'Luke Lafitte',
  '9781644114063',
  'Lafitte''s study of how machine intelligence intersects with the imaginal realm — the dimension in which meaning, symbol, and spirit are negotiated — and what spiritual freedom might look like as matter is re-animated by technique.',
  1,
  'current',
  'How does Lafitte distinguish the imaginal from mere simulation or fantasy? Where do you see the boundary between machine processes and genuine participation in meaning?',
  'Core'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  title = EXCLUDED.title,
  description = EXCLUDED.description,
  discussion_prompt = EXCLUDED.discussion_prompt,
  thematic_arc = EXCLUDED.thematic_arc;

-- ============================================================
-- 2. The Perennial Philosophy — sources for Huxley
-- ============================================================
INSERT INTO public.book_clubs (id, slug, name, description, host_faculty_id, status)
VALUES (
  'perennial-philosophy',
  'perennial-philosophy',
  'The Perennial Philosophy',
  'The Perennial Philosophy reading group (one Matrix space for the series): primary texts Huxley drew upon for his 1945 anthology — Vedanta, Buddhism, Christian mysticism, and Taoist sources — then The Perennial Philosophy as capstone. Led by faculty persona a.AldousHuxley.',
  'a.AldousHuxley',
  'active'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  description = EXCLUDED.description,
  status = EXCLUDED.status,
  host_faculty_id = EXCLUDED.host_faculty_id;

INSERT INTO public.book_club_books (
  club_id, slug, title, author, isbn, description, reading_order, status,
  discussion_prompt, thematic_arc
)
VALUES
(
  'perennial-philosophy',
  'principal-upanishads',
  'The Principal Upanishads',
  'Various (S. Radhakrishnan ed.)',
  '9780872206374',
  'Foundational Vedantic dialogues — Brihadaranyaka, Chandogya, Katha, and others — among the sources Huxley quotes for the non-dual current in Hindu teaching.',
  1,
  'current',
  'What picture of the self (Atman) and ultimate reality (Brahman) emerges here? Which passages most clearly anticipate what Huxley will call the Perennial Philosophy?',
  'Before Huxley: primary texts'
),
(
  'perennial-philosophy',
  'bhagavad-gita',
  'Bhagavad Gita',
  'Trad. auth. Vyasa (trans. e.g. Easwaran)',
  '9781586380199',
  'The great battlefield dialogue on duty, devotion, and knowledge — a central Hindu text Huxley mined for perennial themes of union and discernment.',
  2,
  'upcoming',
  'How do karma, bhakti, and jnana map onto a single path? Where is the "peace that passeth understanding" in this text?',
  'Before Huxley: primary texts'
),
(
  'perennial-philosophy',
  'dhammapada',
  'The Dhammapada',
  'Attributed to the Buddha',
  '9780861712847',
  'Short Buddhist verses on mind, ethics, and liberation — representative of the tradition Huxley cites alongside Vedanta.',
  3,
  'upcoming',
  'What is the role of disciplined attention and compassion in liberation? How does this language pair with Upanishadic insight?',
  'Before Huxley: primary texts'
),
(
  'perennial-philosophy',
  'cloud-of-unknowing',
  'The Cloud of Unknowing',
  'Anonymous (14th c.)',
  '9780140447620',
  'A masterpiece of Christian apophatic mysticism — unknowing as the path toward union — one of Huxley''s Western sources for perennial wisdom.',
  4,
  'upcoming',
  'What does it mean to love God "darkly" beyond concepts? How does this compare to neti neti in the Upanishads?',
  'Before Huxley: primary texts'
),
(
  'perennial-philosophy',
  'the-perennial-philosophy',
  'The Perennial Philosophy',
  'Aldous Huxley',
  '9780060901912',
  'Huxley''s 1945 anthology and essay: quotations and commentary weaving Hindu, Buddhist, Taoist, and Christian mystical sources into one argument about the common metaphysic of spirit.',
  5,
  'upcoming',
  'Having read the sources, how persuasive is Huxley''s braid? What is gained — and what is flattened — by treating these texts as one philosophy?',
  'Capstone: Huxley''s synthesis'
)
ON CONFLICT (club_id, slug) DO UPDATE SET
  title = EXCLUDED.title,
  author = EXCLUDED.author,
  description = EXCLUDED.description,
  discussion_prompt = EXCLUDED.discussion_prompt,
  thematic_arc = EXCLUDED.thematic_arc;
