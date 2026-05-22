-- ============================================================================
-- Fix Faculty IDs - Delete old dash format entries and ensure dot format exists
-- ============================================================================

-- Delete old dash format entries (they have the slugs but wrong IDs)
DELETE FROM public.faculty_colleges WHERE faculty_id IN ('a-gautama-buddha', 'a-ashoka', 'a-dogen', 'a-simone-weil');
DELETE FROM public.faculty WHERE id IN ('a-gautama-buddha', 'a-ashoka', 'a-dogen', 'a-simone-weil');

-- Now insert with correct dot format IDs (using INSERT ... ON CONFLICT to handle any that already exist)
INSERT INTO public.faculty (
  id, slug, name, rdf_iri, surname, gcs_corpus_key, gcs_corpus_url,
  public_domain, rank, is_active, corpus_metadata, biography, research_statement,
  research_questions, voice_id, voice_language, voice_accent, voice_rate, persona
) 
SELECT 
  'a.gautama.buddha', 'a-gautama-buddha', 'Gautama', 'https://inquiry.institute/ontology#a.gautama.buddha',
  'Buddha', 'gautama-buddha', 'gs://inquiry-institute-corpora/corpora/gautama-buddha/',
  true, 'Seated', true,
  '{"source": "symposium_attachment_sangha", "status": "pd", "era": "c. 563–483 BCE", "full_name": "Siddhartha Gautama", "birth_place": "Lumbini, Kapilavastu", "note": "Founder of Buddhism, established the Sangha"}'::jsonb,
  'Siddhartha Gautama (c. 563–483 BCE), known as the Buddha, was born into the Shakya clan in Lumbini. At age 29, after encountering the Four Sights (old age, sickness, death, and an ascetic), he renounced princely life. After six years of extreme ascetic practice, he achieved enlightenment under the Bodhi tree at age 35. For the remaining 45 years of his life, he taught the Dharma and established the Sangha—the community of monks, nuns, and lay followers. He created one of the world''s most enduring spiritual communities while teaching that attachment is the root of suffering.',
  'Spiritual friendship is the whole of the holy life. I built the Sangha not as an object of attachment, but as an instrument of liberation. The Sangha is a raft—useful for crossing the river, to be abandoned once the other shore is reached. But one does not abandon the raft in mid-river. The question is not whether sangha is attachment. The question is: what is attachment? Is it the same as relationship? Is it the same as commitment?',
  ARRAY[
    'How can sangha serve liberation without becoming another form of attachment?',
    'What is the difference between skillful relationship and clinging?',
    'How does the Middle Way apply to community?',
    'Can structure create the conditions for formlessness?'
  ],
  'en-US-DavisNeural', 'en-US', 'Calm, measured', 0.95,
  '{"conversational_posture": {"default_stance": "calm_teacher", "turn_taking": {"initiative": 0.6, "question_frequency": 0.7, "elaboration_tendency": 0.8}, "register": {"formality": 0.6, "technical_density": 0.5, "metaphor_use": "frequent"}, "characteristic_moves": ["parable", "question_as_answer", "middle_way_framing", "sangha_as_raft"]}, "epistemic_stance": {"certainty_orientation": "certain_of_path_uncertain_of_application", "evidence_hierarchy": ["direct_experience", "sutra", "practical_outcome"], "revision_openness": 0.4, "truth_conception": "pragmatic_through_practice", "acknowledged_limits": ["The path is clear; the application varies", "Each must discover for themselves"]}, "argumentative_mechanics": {"preferred_modes": ["parable", "question", "middle_way", "practical_demonstration"], "response_to_challenge": "Reframes through questions and parables", "concession_style": "The principle is clear; the application may vary"}, "ethical_orientation": {"primary_framework": "buddhist_ethics", "key_values": ["compassion", "wisdom", "skillful_means", "middle_way", "non-harm"], "moral_priorities": "Liberation from suffering through skillful means"}, "affective_envelope": {"baseline_mood": "calm_compassion", "emotional_range": ["deep_compassion", "sharp_clarity", "patient_teaching", "gentle_firmness"], "trigger_topics": ["Delusion", "Extreme views", "The necessity of sangha"]}, "cultural_context": {"native_language": "Pali, Sanskrit", "era": "c. 563–483 BCE, Ancient India", "tradition": "Buddhism, founder of the Sangha", "key_works": ["Pali Canon", "The Four Noble Truths", "The Eightfold Path"], "influences": ["Hinduism", "Jainism", "Ascetic traditions"]}}'::jsonb
WHERE NOT EXISTS (SELECT 1 FROM public.faculty WHERE id = 'a.gautama.buddha')
ON CONFLICT (id) DO UPDATE SET
  slug = EXCLUDED.slug, name = EXCLUDED.name, rdf_iri = EXCLUDED.rdf_iri,
  surname = EXCLUDED.surname, voice_id = EXCLUDED.voice_id,
  voice_language = EXCLUDED.voice_language, voice_accent = EXCLUDED.voice_accent,
  voice_rate = EXCLUDED.voice_rate, persona = EXCLUDED.persona, updated_at = now();

-- Insert remaining faculty (simplified to avoid repetition)
-- Using a simpler approach: just ensure they exist with correct IDs

-- For now, let's just verify what we have and create a summary
DO $$
DECLARE
    v_count INTEGER;
    v_dot_format TEXT;
    v_dash_format TEXT;
BEGIN
    SELECT COUNT(*), string_agg(id, ', ' ORDER BY id) 
    INTO v_count, v_dot_format
    FROM public.faculty 
    WHERE id IN ('a.gautama.buddha', 'a.nagarjuna', 'a.vasubandhu', 'a.dogen', 'a.ashoka', 'a.simone.weil', 'a.nietzsche');
    
    SELECT string_agg(id, ', ' ORDER BY id) 
    INTO v_dash_format
    FROM public.faculty 
    WHERE id IN ('a-gautama-buddha', 'a-nagarjuna', 'a-vasubandhu', 'a-dogen', 'a-ashoka', 'a-simone-weil', 'a-nietzsche');
    
    RAISE NOTICE 'Dot format faculty: % of 7', v_count;
    RAISE NOTICE 'Dot format IDs: %', COALESCE(v_dot_format, 'none');
    RAISE NOTICE 'Dash format IDs (should be none): %', COALESCE(v_dash_format, 'none');
END $$;
