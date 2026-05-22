-- Migration: Add Encyclopædia Faculty Members (complete)
-- These faculty members are needed as canonical authors for The Encyclopædia, Volume I: Mind
-- Date: 2026-01-29

-- Note: Only inserting faculty that don't already exist
-- Using ON CONFLICT DO NOTHING to safely handle any that already exist
-- Including rdf_iri which is required

-- Henri Bergson - Consciousness, Memory
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.bergson',
  'a-bergson',
  'https://inquiry.institute/faculty/a.bergson',
  'Henri',
  'Bergson',
  'professor',
  true,
  ARRAY['Philosophy', 'Mind'],
  'Henri Bergson (1859-1941) was a French philosopher known for his influential contributions to philosophy of mind, especially his concepts of duration (durée) and intuition. His work "Matter and Memory" explores the relationship between body and mind, while "Creative Evolution" introduced the concept of élan vital. He won the Nobel Prize in Literature in 1927.'
) ON CONFLICT (id) DO NOTHING;

-- Jean Piaget - Intelligence
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.piaget',
  'a-piaget',
  'https://inquiry.institute/faculty/a.piaget',
  'Jean',
  'Piaget',
  'professor',
  true,
  ARRAY['Psychology', 'Education', 'Epistemology'],
  'Jean Piaget (1896-1980) was a Swiss psychologist known for his pioneering work in child development and genetic epistemology. His theory of cognitive development describes how children construct a mental model of the world through stages of sensorimotor, preoperational, concrete operational, and formal operational thinking.'
) ON CONFLICT (id) DO NOTHING;

-- Edmund Husserl - Awareness
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.husserl',
  'a-husserl',
  'https://inquiry.institute/faculty/a.husserl',
  'Edmund',
  'Husserl',
  'professor',
  true,
  ARRAY['Philosophy', 'Phenomenology'],
  'Edmund Husserl (1859-1938) was a German philosopher who established phenomenology as a philosophical movement. His work on intentionality, the structure of consciousness, and the phenomenological method profoundly influenced 20th-century philosophy, including existentialism and hermeneutics.'
) ON CONFLICT (id) DO NOTHING;

-- Ulric Neisser - Cognition
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.neisser',
  'a-neisser',
  'https://inquiry.institute/faculty/a.neisser',
  'Ulric',
  'Neisser',
  'professor',
  true,
  ARRAY['Psychology', 'Cognitive Science'],
  'Ulric Neisser (1928-2012) was a German-American psychologist who has been called the "father of cognitive psychology." His 1967 book "Cognitive Psychology" helped launch the cognitive revolution in psychology. He later advocated for ecological approaches to cognition, emphasizing real-world contexts.'
) ON CONFLICT (id) DO NOTHING;

-- Sigmund Freud - Dream
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.freud',
  'a-freud',
  'https://inquiry.institute/faculty/a.freud',
  'Sigmund',
  'Freud',
  'professor',
  true,
  ARRAY['Psychology', 'Psychoanalysis'],
  'Sigmund Freud (1856-1939) was an Austrian neurologist and the founder of psychoanalysis. His "Interpretation of Dreams" (1900) proposed that dreams are the "royal road to the unconscious." His theories of the unconscious mind, defense mechanisms, and dream symbolism have had lasting influence on psychology and culture.'
) ON CONFLICT (id) DO NOTHING;

-- Samuel Taylor Coleridge - Imagination
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.coleridge',
  'a-coleridge',
  'https://inquiry.institute/faculty/a.coleridge',
  'Samuel Taylor',
  'Coleridge',
  'professor',
  true,
  ARRAY['Literature', 'Philosophy', 'Criticism'],
  'Samuel Taylor Coleridge (1772-1834) was an English poet, literary critic, and philosopher. His "Biographia Literaria" (1817) distinguishes between primary imagination (living perception) and secondary imagination (creative power). His poems "The Rime of the Ancient Mariner" and "Kubla Khan" exemplify romantic imagination.'
) ON CONFLICT (id) DO NOTHING;

-- Maurice Merleau-Ponty - Perception
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.merleauponty',
  'a-merleauponty',
  'https://inquiry.institute/faculty/a.merleauponty',
  'Maurice',
  'Merleau-Ponty',
  'professor',
  true,
  ARRAY['Philosophy', 'Phenomenology'],
  'Maurice Merleau-Ponty (1908-1961) was a French phenomenological philosopher known for his work on perception and embodiment. His "Phenomenology of Perception" (1945) argues that perception is the primordial contact with the world, grounded in bodily engagement rather than abstract thought.'
) ON CONFLICT (id) DO NOTHING;

-- Ernst Weber - Sensation (using a.eweber to avoid conflict with Max Weber)
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.eweber',
  'a-eweber',
  'https://inquiry.institute/faculty/a.eweber',
  'Ernst Heinrich',
  'Weber',
  'professor',
  true,
  ARRAY['Physiology', 'Psychology'],
  'Ernst Heinrich Weber (1795-1878) was a German physician who is considered one of the founders of experimental psychology. Weber''s Law, which he formulated in 1834, describes the relationship between the magnitude of a physical stimulus and its perceived intensity, establishing a foundation for psychophysics.'
) ON CONFLICT (id) DO NOTHING;

-- Hannah Arendt - Thought
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.arendt',
  'a-arendt',
  'https://inquiry.institute/faculty/a.arendt',
  'Hannah',
  'Arendt',
  'professor',
  true,
  ARRAY['Philosophy', 'Political Theory'],
  'Hannah Arendt (1906-1975) was a German-American political theorist. Her work "The Life of the Mind" explores thinking, willing, and judging as the fundamental activities of the mind. She is also known for her analysis of totalitarianism and her concept of the "banality of evil."'
) ON CONFLICT (id) DO NOTHING;

-- Arthur Schopenhauer - Will
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.schopenhauer',
  'a-schopenhauer',
  'https://inquiry.institute/faculty/a.schopenhauer',
  'Arthur',
  'Schopenhauer',
  'professor',
  true,
  ARRAY['Philosophy'],
  'Arthur Schopenhauer (1788-1860) was a German philosopher known for his pessimistic philosophy. His magnum opus, "The World as Will and Representation" (1818), presents the will as the fundamental reality underlying all phenomena. His ideas influenced Nietzsche, Freud, and many others.'
) ON CONFLICT (id) DO NOTHING;

-- Jakob von Uexküll - Animal Mind
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.uexkull',
  'a-uexkull',
  'https://inquiry.institute/faculty/a.uexkull',
  'Jakob',
  'von Uexküll',
  'professor',
  true,
  ARRAY['Biology', 'Biosemiotics'],
  'Jakob Johann von Uexküll (1864-1944) was a Baltic German biologist who founded the field of biosemiotics. His concept of Umwelt describes the perceptual world unique to each species—a self-contained bubble of meaning constituted by an organism''s sensory and motor capabilities.'
) ON CONFLICT (id) DO NOTHING;

-- Blaise Pascal - Uncertainty
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.pascal',
  'a-pascal',
  'https://inquiry.institute/faculty/a.pascal',
  'Blaise',
  'Pascal',
  'professor',
  true,
  ARRAY['Mathematics', 'Philosophy', 'Theology'],
  'Blaise Pascal (1623-1662) was a French mathematician, physicist, and religious philosopher. His "Pensées" explores the human condition caught between infinities, the famous wager on God''s existence, and the limits of reason. He also made foundational contributions to probability theory and hydraulics.'
) ON CONFLICT (id) DO NOTHING;

-- Nicholas of Cusa - Not-Knowing
INSERT INTO public.faculty (id, slug, rdf_iri, name, surname, rank, public_domain, fields, biography)
VALUES (
  'a.cusa',
  'a-cusa',
  'https://inquiry.institute/faculty/a.cusa',
  'Nicholas',
  'of Cusa',
  'professor',
  true,
  ARRAY['Philosophy', 'Theology', 'Mathematics'],
  'Nicholas of Cusa (1401-1464) was a German philosopher, theologian, and cardinal. His concept of "docta ignorantia" (learned ignorance) argues that the more precisely we approach the infinite, the more our knowledge reveals its own limits—a coincidence of opposites that points beyond rational comprehension.'
) ON CONFLICT (id) DO NOTHING;

-- Add comment
COMMENT ON TABLE public.faculty IS 'Faculty members including canonical authors for The Encyclopædia';
