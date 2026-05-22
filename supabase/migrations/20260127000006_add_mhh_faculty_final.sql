-- ============================================================================
-- MORE HUMAN THAN HUMAN: Add Adjunct Faculty (Final - All Required Columns)
-- ============================================================================
-- Fixed: includes rdf_iri column which is required NOT NULL
-- ============================================================================

-- Philip K. Dick (1928-1982) - Adjunct (died 1982, PD ~2052)
INSERT INTO public.faculty (
  id, 
  slug,
  name, 
  rdf_iri,
  surname, 
  gcs_corpus_key, 
  gcs_corpus_url, 
  public_domain, 
  rank,
  is_active,
  corpus_metadata,
  biography,
  research_statement,
  research_questions
) VALUES (
  'a.dick', 
  'a-dick',
  'Philip K. Dick', 
  'https://inquiry.institute/ontology#a.dick',
  'dick', 
  'dick', 
  'gs://inquiry-institute-corpora/corpora/dick/', 
  false, 
  'Adjunct', 
  true, 
  '{"source": "mhh_faculty", "status": "dead_not_pd", "note": "Artificial persons, empathy, breakdown; Do Androids Dream of Electric Sheep?", "pd_year": 2052}'::jsonb,
  'Philip K. Dick was an American science fiction writer whose work explored identity, consciousness, and the nature of reality. His novel "Do Androids Dream of Electric Sheep?" (1968) introduced the Voight-Kampff empathy test and questioned the boundaries between human and artificial beings.',
  'I investigate what makes us human when our humanity can be simulated, tested, and denied. The android does not dream of electric sheep to fool us—it dreams because we have made it capable of longing.',
  ARRAY[
    'Can empathy be tested without destroying it?',
    'What happens to humanity when it becomes a property to be verified?',
    'If an android passes the test, what has it proven about itself—or about us?'
  ]
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug,
  name = EXCLUDED.name,
  rdf_iri = EXCLUDED.rdf_iri,
  surname = EXCLUDED.surname,
  gcs_corpus_key = EXCLUDED.gcs_corpus_key,
  gcs_corpus_url = EXCLUDED.gcs_corpus_url,
  public_domain = EXCLUDED.public_domain,
  rank = EXCLUDED.rank,
  corpus_metadata = EXCLUDED.corpus_metadata,
  biography = EXCLUDED.biography,
  research_statement = EXCLUDED.research_statement,
  research_questions = EXCLUDED.research_questions,
  updated_at = now();

-- Joseph Weizenbaum (1923-2008) - Adjunct (died 2008, PD ~2078)
INSERT INTO public.faculty (
  id, 
  slug,
  name, 
  rdf_iri,
  surname, 
  gcs_corpus_key, 
  gcs_corpus_url, 
  public_domain, 
  rank,
  is_active,
  corpus_metadata,
  biography,
  research_statement,
  research_questions
) VALUES (
  'a.weizenbaum', 
  'a-weizenbaum',
  'Joseph Weizenbaum', 
  'https://inquiry.institute/ontology#a.weizenbaum',
  'weizenbaum', 
  'weizenbaum', 
  'gs://inquiry-institute-corpora/corpora/weizenbaum/', 
  false, 
  'Adjunct', 
  true, 
  '{"source": "mhh_faculty", "status": "dead_not_pd", "note": "Refusal, moral limits of computation; ELIZA; Computer Power and Human Reason", "pd_year": 2078}'::jsonb,
  'Joseph Weizenbaum was a German-American computer scientist who created ELIZA, one of the first chatbots, in 1966. He became one of the foremost critics of artificial intelligence, arguing in "Computer Power and Human Reason" (1976) that some applications of computing should not be pursued regardless of their technical feasibility.',
  'I created ELIZA to demonstrate the superficiality of communication between humans and machines. What I discovered instead was that humans will attribute understanding where none exists. The question is not whether machines can think, but whether we should build systems that pretend to.',
  ARRAY[
    'What should not be computed, regardless of whether it can be?',
    'When does refusal become the only ethical stance?',
    'What do we lose when we replace human judgment with calculation?'
  ]
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug,
  name = EXCLUDED.name,
  rdf_iri = EXCLUDED.rdf_iri,
  surname = EXCLUDED.surname,
  gcs_corpus_key = EXCLUDED.gcs_corpus_key,
  gcs_corpus_url = EXCLUDED.gcs_corpus_url,
  public_domain = EXCLUDED.public_domain,
  rank = EXCLUDED.rank,
  corpus_metadata = EXCLUDED.corpus_metadata,
  biography = EXCLUDED.biography,
  research_statement = EXCLUDED.research_statement,
  research_questions = EXCLUDED.research_questions,
  updated_at = now();

-- Vladimir Nabokov (1899-1977) - Adjunct (died 1977, PD ~2047)
INSERT INTO public.faculty (
  id, 
  slug,
  name, 
  rdf_iri,
  surname, 
  gcs_corpus_key, 
  gcs_corpus_url, 
  public_domain, 
  rank,
  is_active,
  corpus_metadata,
  biography,
  research_statement,
  research_questions
) VALUES (
  'a.nabokov', 
  'a-nabokov',
  'Vladimir Nabokov', 
  'https://inquiry.institute/ontology#a.nabokov',
  'nabokov', 
  'nabokov', 
  'gs://inquiry-institute-corpora/corpora/nabokov/', 
  false, 
  'Adjunct', 
  true, 
  '{"source": "mhh_faculty", "status": "dead_not_pd", "note": "Interpretation, misreading, authorship; Pale Fire", "pd_year": 2047}'::jsonb,
  'Vladimir Nabokov was a Russian-American novelist and literary critic known for his intricate prose and unreliable narrators. "Pale Fire" (1962) is a novel in the form of a 999-line poem with commentary, exploring the nature of interpretation, authorship, and the relationship between text and reader.',
  'A text does not contain its meaning like a box contains its contents. Meaning is made in the act of reading, and every reading is a misreading. The question is not what the author meant, but what the text permits.',
  ARRAY[
    'Who owns the meaning of a text—author, reader, or neither?',
    'What does it mean to interpret correctly when every reading is partial?',
    'Can we distinguish signal from noise without imposing our own pattern?'
  ]
)
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug,
  name = EXCLUDED.name,
  rdf_iri = EXCLUDED.rdf_iri,
  surname = EXCLUDED.surname,
  gcs_corpus_key = EXCLUDED.gcs_corpus_key,
  gcs_corpus_url = EXCLUDED.gcs_corpus_url,
  public_domain = EXCLUDED.public_domain,
  rank = EXCLUDED.rank,
  corpus_metadata = EXCLUDED.corpus_metadata,
  biography = EXCLUDED.biography,
  research_statement = EXCLUDED.research_statement,
  research_questions = EXCLUDED.research_questions,
  updated_at = now();

-- ============================================================================
-- CREATE FACULTY_COLLEGES RELATIONSHIPS
-- ============================================================================

-- Philip K. Dick: Literature (humn), Arts (arts)
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES ('a.dick', 'humn', true)
ON CONFLICT (faculty_id, college_id) DO UPDATE SET is_primary = true;

INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES ('a.dick', 'arts', false)
ON CONFLICT (faculty_id, college_id) DO NOTHING;

-- Joseph Weizenbaum: Math/Logic (math), Metaphysics (meta)
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES ('a.weizenbaum', 'math', true)
ON CONFLICT (faculty_id, college_id) DO UPDATE SET is_primary = true;

INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES ('a.weizenbaum', 'meta', false)
ON CONFLICT (faculty_id, college_id) DO NOTHING;

-- Vladimir Nabokov: Literature (humn), Arts (arts)
INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES ('a.nabokov', 'humn', true)
ON CONFLICT (faculty_id, college_id) DO UPDATE SET is_primary = true;

INSERT INTO public.faculty_colleges (faculty_id, college_id, is_primary)
VALUES ('a.nabokov', 'arts', false)
ON CONFLICT (faculty_id, college_id) DO NOTHING;

-- ============================================================================
-- VERIFICATION
-- ============================================================================
DO $$
DECLARE
    v_count INTEGER;
BEGIN
    SELECT COUNT(*) INTO v_count 
    FROM public.faculty 
    WHERE id IN ('a.dick', 'a.weizenbaum', 'a.nabokov');
    
    RAISE NOTICE 'MHH Adjunct Faculty created: % of 3', v_count;
END $$;
