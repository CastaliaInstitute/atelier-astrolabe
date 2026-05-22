-- Migration: Add Missing Encyclopædia Faculty Members
-- These faculty members are needed for Volume 1 constellation entries
-- Date: 2026-01-30

-- Nāgārjuna - Self (Buddhist perspective)
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.nagarjuna',
  'a-nagarjuna',
  'https://inquiry.institute/faculty/a.nagarjuna',
  'Nāgārjuna',
  '',
  'professor',
  true,
  ARRAY['Philosophy', 'Buddhism', 'Metaphysics'],
  'Nāgārjuna (c. 150-250 CE) was an Indian Buddhist philosopher and founder of the Madhyamaka (Middle Way) school. His "Mūlamadhyamakakārikā" (Fundamental Verses on the Middle Way) argues that all phenomena are empty (śūnyatā) of inherent existence, establishing the doctrine of dependent origination (pratītyasamutpāda) as the core of Mahāyāna philosophy.'
) ON CONFLICT (id) DO NOTHING;

-- Paul Ricoeur - Self (Narrative perspective)
-- Note: Ricoeur died in 2005 → works are under copyright (NOT public domain)
-- Must be handled via licensed excerpts, secondary commentary, or AI-generated
-- dialogic paraphrase. gutenberg_author_id is NULL (not in Gutenberg).
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography, gutenberg_author_id)
VALUES (
  'a.ricoeur',
  'a-ricoeur',
  'https://inquiry.institute/faculty/a.ricoeur',
  'Paul',
  'Ricoeur',
  'professor',
  false,  -- NOT public domain - died 2005, works under copyright
  ARRAY['Philosophy', 'Hermeneutics', 'Narrative Theory'],
  'Paul Ricoeur (1913-2005) was a French philosopher known for his work in hermeneutics, phenomenology, and narrative theory. His "Oneself as Another" (1990) explores the self through narrative identity, arguing that we understand ourselves through the stories we tell. His work bridges continental and analytic philosophy.',
  NULL  -- Not in Gutenberg (copyrighted)
) ON CONFLICT (id) DO NOTHING;

-- Meister Eckhart - Consciousness (Mystical perspective)
-- Note: Works are in public domain (died 1328) but may not be in Project Gutenberg
-- Gutenberg focuses primarily on English-language texts, and Eckhart's works are mostly
-- in Middle High German/Latin. Translations may exist but need verification.
-- gutenberg_author_id left as NULL - to be populated if/when verified
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography, gutenberg_author_id)
VALUES (
  'a.eckhart',
  'a-eckhart',
  'https://inquiry.institute/faculty/a.eckhart',
  'Meister',
  'Eckhart',
  'professor',
  true,
  ARRAY['Philosophy', 'Theology', 'Mysticism'],
  'Meister Eckhart (c. 1260-1328) was a German Dominican theologian and mystic. His sermons and treatises explore the relationship between the soul and God, emphasizing the "birth of God in the soul" and the "ground" (Grunt) of being. His work influenced later mystics and philosophers, including Heidegger.',
  NULL  -- TODO: Verify if English translations exist in Gutenberg
) ON CONFLICT (id) DO NOTHING;

-- Add comment
COMMENT ON TABLE public.faculty IS 'Faculty members including canonical authors for The Encyclopædia';
