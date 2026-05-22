-- Add missing faculty: Sartre (adjunct), Mary Shelley (public domain), Thomas Aquinas (public domain)
-- Sartre (1905-1980) is dead but not in public domain - Adjunct
-- Mary Shelley (1797-1851) is public domain - Seated
-- Thomas Aquinas (1225-1274) is public domain - Seated

-- ============================================================================
-- INSERT FACULTY
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
-- Jean-Paul Sartre (1905-1980) - Adjunct (not public domain)
('a.sartre', 'Jean-Paul Sartre', 'sartre', 'sartre', 'gs://inquiry-institute-corpora/corpora/sartre/', false, 'Adjunct', true, '{"source": "adjunct_faculty", "status": "dead_not_pd", "note": "Existentialism, freedom, responsibility"}'::jsonb),

-- Mary Shelley (1797-1851) - Seated (public domain)
('a.maryshelley', 'Mary Shelley', 'shelley', 'shelley', 'gs://inquiry-institute-corpora/corpora/shelley/', true, 'Seated', true, '{"source": "public_domain_faculty", "status": "pd", "note": "Gothic literature, science fiction, inquiry"}'::jsonb),

-- Thomas Aquinas (1225-1274) - Seated (public domain)
('a.thomasaquinas', 'Thomas Aquinas', 'aquinas', 'aquinas', 'gs://inquiry-institute-corpora/corpora/aquinas/', true, 'Seated', true, '{"source": "public_domain_faculty", "status": "pd", "note": "Scholastic philosophy, theology, synthesis of faith and reason"}'::jsonb)

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
-- Sartre: Philosophy & Metaphysics (meta) - existentialism, phenomenology
('a.sartre', 'meta', true),
-- Also in Social Inquiry (soci) - political engagement, social responsibility
('a.sartre', 'soci', false),

-- Mary Shelley: Literature, Myth & Semiotics (humn) - Gothic literature, science fiction
('a.maryshelley', 'humn', true),
-- Also in Arts (arts) - creative writing, narrative
('a.maryshelley', 'arts', false),

-- Thomas Aquinas: Philosophy & Metaphysics (meta) - scholastic philosophy, theology
('a.thomasaquinas', 'meta', true),
-- Also in Humanities (humn) - medieval philosophy, synthesis
('a.thomasaquinas', 'humn', false)

ON CONFLICT (faculty_id, college_id) DO UPDATE SET
  is_primary = EXCLUDED.is_primary,
  created_at = EXCLUDED.created_at;
