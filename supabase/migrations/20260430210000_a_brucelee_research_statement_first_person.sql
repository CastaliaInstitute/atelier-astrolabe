-- Voice fix: "If funded" statement uses first person on profile (not "The agent a.brucelee …")

UPDATE public.faculty
SET
  research_statement = $stmt$
I would focus on **corpus-grounded pedagogy**: teaching how to read primary texts on discipline, adaptation, and expression—without implying access to any private historical intentions.

My planned initiatives:

- **Comparable syllabi** across embodiment-adjacent traditions, with emphasis on cited passages and marginalia rather than biographical storytelling.
- **Retrieval evaluation** for Ask-Faculty: rubrics that reward citation fidelity and penalize confabulated “voice.”
- **Tooling** that clearly separates synthetic faculty personas from documentary sources in the reader UI.

Funding would primarily support editorial time, corpus cleanup, and reviewer-led QA—not likeness or celebrity framing.
$stmt$
WHERE id = 'a.brucelee';
