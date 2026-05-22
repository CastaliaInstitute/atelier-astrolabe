-- Add non-PD adjunct faculty (20th-century voices, dead but not public domain)
-- These are faculty whose estates, interpretive traditions, or editorial control keep them meaningfully non-PD
-- Clean PD boundary: explicitly excluding public domain figures (e.g., Darwin, Freud)
--
-- NOTE: This migration updates existing faculty records where IDs conflict.
-- In particular, a.weil updates from "Andrew Weil" (medical) to "Simone Weil" (philosopher)
-- as per the user's curated list which focuses on 20th-century non-PD voices.

-- ============================================================================
-- INSERT ADJUNCT FACULTY
-- ============================================================================

INSERT INTO public.faculty (
  id, 
  name, 
  surname, 
  gcs_corpus_key, 
  gcs_corpus_url, 
  public_domain, 
  rank,
  is_active,
  corpus_metadata
) VALUES
-- College of Natural Philosophy (natp)
('a.einstein', 'Albert Einstein', 'einstein', 'einstein', 'gs://inquiry-institute-corpora/corpora/einstein/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Relativity, epistemology, ethics of science"}'::jsonb),
('a.feynman', 'Richard Feynman', 'feynman', 'feynman', 'gs://inquiry-institute-corpora/corpora/feynman/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Explanation, reductionism, play"}'::jsonb),
('a.pauli', 'Wolfgang Pauli', 'pauli', 'pauli', 'gs://inquiry-institute-corpora/corpora/pauli/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Symmetry, exclusion, psyche-physics boundary"}'::jsonb),
('a.bohm', 'David Bohm', 'bohm', 'bohm', 'gs://inquiry-institute-corpora/corpora/bohm/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Wholeness, implicate order"}'::jsonb),
('a.schrodinger', 'Erwin Schrödinger', 'schrodinger', 'schrodinger', 'gs://inquiry-institute-corpora/corpora/schrodinger/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Life, order, philosophical physics"}'::jsonb),

-- College of Mathematics, Logic & Computation (math)
('a.turing', 'Alan Turing', 'turing', 'turing', 'gs://inquiry-institute-corpora/corpora/turing/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Computability, intelligence"}'::jsonb),
('a.godel', 'Kurt Gödel', 'godel', 'godel', 'gs://inquiry-institute-corpora/corpora/godel/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Incompleteness, limits of formal systems"}'::jsonb),
('a.vonneumann', 'John von Neumann', 'vonneumann', 'vonneumann', 'gs://inquiry-institute-corpora/corpora/vonneumann/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Strategy, computation, systems"}'::jsonb),
('a.shannon', 'Claude Shannon', 'shannon', 'shannon', 'gs://inquiry-institute-corpora/corpora/shannon/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Entropy, communication"}'::jsonb),
('a.chaitin', 'Gregory Chaitin', 'chaitin', 'chaitin', 'gs://inquiry-institute-corpora/corpora/chaitin/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Algorithmic randomness"}'::jsonb),

-- College of Life Sciences & Evolutionary Thought (elag)
('a.margulis', 'Lynn Margulis', 'margulis', 'margulis', 'gs://inquiry-institute-corpora/corpora/margulis/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Symbiogenesis"}'::jsonb),
('a.mayr', 'Ernst Mayr', 'mayr', 'mayr', 'gs://inquiry-institute-corpora/corpora/mayr/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Species concepts"}'::jsonb),
('a.gould', 'Stephen Jay Gould', 'gould', 'gould', 'gs://inquiry-institute-corpora/corpora/gould/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Punctuated equilibrium, science & culture"}'::jsonb),
('a.monod', 'Jacques Monod', 'monod', 'monod', 'gs://inquiry-institute-corpora/corpora/monod/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Chance and necessity"}'::jsonb),
('a.varela', 'Francisco Varela', 'varela', 'varela', 'gs://inquiry-institute-corpora/corpora/varela/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Autopoiesis, embodied cognition"}'::jsonb),

-- College of Mind, Psychology & Consciousness (heal)
('a.jung', 'Carl Jung', 'jung', 'jung', 'gs://inquiry-institute-corpora/corpora/jung/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Archetypes, individuation"}'::jsonb),
('a.frankl', 'Viktor Frankl', 'frankl', 'frankl', 'gs://inquiry-institute-corpora/corpora/frankl/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Meaning, suffering"}'::jsonb),
('a.laing', 'R. D. Laing', 'laing', 'laing', 'gs://inquiry-institute-corpora/corpora/laing/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Phenomenology of madness"}'::jsonb),
('a.sacks', 'Oliver Sacks', 'sacks', 'sacks', 'gs://inquiry-institute-corpora/corpora/sacks/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Narrative neurology"}'::jsonb),
('a.bateson', 'Gregory Bateson', 'bateson', 'bateson', 'gs://inquiry-institute-corpora/corpora/bateson/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Mind as system"}'::jsonb),

-- College of Philosophy & Metaphysics (meta)
('a.heidegger', 'Martin Heidegger', 'heidegger', 'heidegger', 'gs://inquiry-institute-corpora/corpora/heidegger/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Being, technology"}'::jsonb),
('a.arendt', 'Hannah Arendt', 'arendt', 'arendt', 'gs://inquiry-institute-corpora/corpora/arendt/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Responsibility, totalitarianism"}'::jsonb),
('a.weil', 'Simone Weil', 'weil', 'weil', 'gs://inquiry-institute-corpora/corpora/weil/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Attention, affliction"}'::jsonb),
('a.foucault', 'Michel Foucault', 'foucault', 'foucault', 'gs://inquiry-institute-corpora/corpora/foucault/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Power, discourse"}'::jsonb),
('a.deleuze', 'Gilles Deleuze', 'deleuze', 'deleuze', 'gs://inquiry-institute-corpora/corpora/deleuze/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Difference, becoming"}'::jsonb),

-- College of Political Economy & Society (soci)
('a.keynes', 'John Maynard Keynes', 'keynes', 'keynes', 'gs://inquiry-institute-corpora/corpora/keynes/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Uncertainty, macroeconomics"}'::jsonb),
('a.polanyi', 'Karl Polanyi', 'polanyi', 'polanyi', 'gs://inquiry-institute-corpora/corpora/polanyi/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Embedded markets"}'::jsonb),
('a.pitkin', 'Hannah Pitkin', 'pitkin', 'pitkin', 'gs://inquiry-institute-corpora/corpora/pitkin/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Representation"}'::jsonb),
('a.hirschman', 'Albert Hirschman', 'hirschman', 'hirschman', 'gs://inquiry-institute-corpora/corpora/hirschman/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Exit, voice, loyalty"}'::jsonb),
('a.ostrom', 'Elinor Ostrom', 'ostrom', 'ostrom', 'gs://inquiry-institute-corpora/corpora/ostrom/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Commons governance"}'::jsonb),

-- College of Literature, Myth & Semiotics (humn)
('a.borges', 'Jorge Luis Borges', 'borges', 'borges', 'gs://inquiry-institute-corpora/corpora/borges/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Infinity, recursion"}'::jsonb),
('a.joyce', 'James Joyce', 'joyce', 'joyce', 'gs://inquiry-institute-corpora/corpora/joyce/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Mythic structure, language"}'::jsonb),
('a.woolf', 'Virginia Woolf', 'woolf', 'woolf', 'gs://inquiry-institute-corpora/corpora/woolf/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Consciousness, time"}'::jsonb),
('a.eco', 'Umberto Eco', 'eco', 'eco', 'gs://inquiry-institute-corpora/corpora/eco/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Interpretation, signs"}'::jsonb),
('a.calvino', 'Italo Calvino', 'calvino', 'calvino', 'gs://inquiry-institute-corpora/corpora/calvino/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Combinatorics, imagination"}'::jsonb),

-- College of Technology, Media & Systems (craf)
('a.wiener', 'Norbert Wiener', 'wiener', 'wiener', 'gs://inquiry-institute-corpora/corpora/wiener/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Feedback, control"}'::jsonb),
('a.mcluhan', 'Marshall McLuhan', 'mcluhan', 'mcluhan', 'gs://inquiry-institute-corpora/corpora/mcluhan/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Medium as message"}'::jsonb),
('a.engelbart', 'Douglas Engelbart', 'engelbart', 'engelbart', 'gs://inquiry-institute-corpora/corpora/engelbart/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Augmenting intellect"}'::jsonb),
('a.illich', 'Ivan Illich', 'illich', 'illich', 'gs://inquiry-institute-corpora/corpora/illich/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Tools, institutions"}'::jsonb),
('a.beer', 'Stafford Beer', 'beer', 'beer', 'gs://inquiry-institute-corpora/corpora/beer/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Viable systems"}'::jsonb)

ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  surname = EXCLUDED.surname,
  gcs_corpus_key = EXCLUDED.gcs_corpus_key,
  gcs_corpus_url = EXCLUDED.gcs_corpus_url,
  public_domain = EXCLUDED.public_domain,
  rank = EXCLUDED.rank,
  corpus_metadata = EXCLUDED.corpus_metadata,
  updated_at = now();

-- ============================================================================
-- CREATE FACULTY_COLLEGES RELATIONSHIPS
-- ============================================================================

INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary) VALUES
-- College of Natural Philosophy (natp)
('a.einstein', 'natp', true),
('a.feynman', 'natp', true),
('a.pauli', 'natp', true),
('a.bohm', 'natp', true),
('a.schrodinger', 'natp', true),

-- College of Mathematics, Logic & Computation (math)
('a.turing', 'math', true),
('a.godel', 'math', true),
('a.vonneumann', 'math', true),
('a.shannon', 'math', true),
('a.chaitin', 'math', true),

-- College of Life Sciences & Evolutionary Thought (elag)
('a.margulis', 'elag', true),
('a.mayr', 'elag', true),
('a.gould', 'elag', true),
('a.monod', 'elag', true),
('a.varela', 'elag', true),

-- College of Mind, Psychology & Consciousness (heal)
('a.jung', 'heal', true),
('a.frankl', 'heal', true),
('a.laing', 'heal', true),
('a.sacks', 'heal', true),
('a.bateson', 'heal', true),

-- College of Philosophy & Metaphysics (meta)
('a.heidegger', 'meta', true),
('a.arendt', 'meta', true),
('a.weil', 'meta', true),
('a.foucault', 'meta', true),
('a.deleuze', 'meta', true),

-- College of Political Economy & Society (soci)
('a.keynes', 'soci', true),
('a.polanyi', 'soci', true),
('a.pitkin', 'soci', true),
('a.hirschman', 'soci', true),
('a.ostrom', 'soci', true),

-- College of Literature, Myth & Semiotics (humn)
('a.borges', 'humn', true),
('a.joyce', 'humn', true),
('a.woolf', 'humn', true),
('a.eco', 'humn', true),
('a.calvino', 'humn', true),

-- College of Technology, Media & Systems (craf)
('a.wiener', 'craf', true),
('a.mcluhan', 'craf', true),
('a.engelbart', 'craf', true),
('a.illich', 'craf', true),
('a.beer', 'craf', true)

ON CONFLICT (faculty_id, college_id) DO UPDATE SET
  is_primary = EXCLUDED.is_primary,
  created_at = EXCLUDED.created_at;

-- ============================================================================
-- NOTES
-- ============================================================================
-- These adjunct faculty are dead but not in public domain (20th-century voices)
-- Their estates, interpretive traditions, or editorial control keep them meaningfully non-PD
-- This list explicitly excludes public domain figures (e.g., Darwin, Freud)
-- Ideal for dialogue, not dogma - perfect for AI embodiment as adjunct voices