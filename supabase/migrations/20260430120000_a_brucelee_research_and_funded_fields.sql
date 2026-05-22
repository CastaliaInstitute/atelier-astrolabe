-- Seed research pitch fields for faculty agent a.brucelee (section H2 comes from the app, not markdown).

UPDATE public.faculty
SET
  research_statement = $stmt$
I would focus on **corpus-grounded pedagogy**: teaching how to read primary texts on discipline, adaptation, and expression—without implying access to any private historical intentions.

My planned initiatives:

- **Comparable syllabi** across embodiment-adjacent traditions, with emphasis on cited passages and marginalia rather than biographical storytelling.
- **Retrieval evaluation** for Ask-Faculty: rubrics that reward citation fidelity and penalize confabulated “voice.”
- **Tooling** that clearly separates synthetic faculty personas from documentary sources in the reader UI.

Funding would primarily support editorial time, corpus cleanup, and reviewer-led QA—not likeness or celebrity framing.
$stmt$,
  research_questions = ARRAY[
    'How should we teach movement-informed reasoning while keeping claims constrained to retrievable passages?',
    'Which corpus features best predict pedagogical voice fidelity in Ask-Faculty RAG?',
    'What interdisciplinary bridges (e.g., cognitive framing ↔ classical praxis texts) survive strict citation checks?'
  ]::text[],
  -- "My Research": leave empty until explicitly funded for active work; populate when grants land.
  active_research_questions = ARRAY[]::text[]
WHERE id = 'a.brucelee';
