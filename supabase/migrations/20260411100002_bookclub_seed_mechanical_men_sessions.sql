-- Seed: Discussion sessions for the Mechanical Men book club
-- Weekly sessions, Wednesdays at 7pm UTC, starting with R.U.R.

-- ============================================================
-- R.U.R. Sessions (current book — 3 weeks)
-- ============================================================

-- Get the R.U.R. book ID
DO $$
DECLARE
  v_rur_id uuid;
  v_newts_id uuid;
BEGIN
  SELECT id INTO v_rur_id FROM public.book_club_books
    WHERE club_id = 'mechanical-men' AND slug = 'rur';

  SELECT id INTO v_newts_id FROM public.book_club_books
    WHERE club_id = 'mechanical-men' AND slug = 'war-with-the-newts';

  -- R.U.R. Session 1: The Factory
  INSERT INTO public.book_club_sessions
    (book_id, title, description, scheduled_at, duration_minutes, chapter_range, discussion_questions, status)
  VALUES (
    v_rur_id,
    'The Factory and the Question of Labor',
    'Our first session covers the opening acts of R.U.R. — the factory tour, Helena Glory''s arrival, and the philosophical debate about the nature of robot labor.',
    '2026-02-19 19:00:00+00',
    75,
    'Prologue and Act I',
    ARRAY[
      'Domin claims the robots "have no will of their own." How does Čapek undermine this claim even in the first act?',
      'Helena arrives as an advocate for robot rights. Is her compassion presented as naive, noble, or something more complex?',
      'The factory produces beings indistinguishable from humans. At what point does a manufactured being deserve rights?'
    ],
    'scheduled'
  ) ON CONFLICT DO NOTHING;

  -- R.U.R. Session 2: The Revolt
  INSERT INTO public.book_club_sessions
    (book_id, title, description, scheduled_at, duration_minutes, chapter_range, discussion_questions, status)
  VALUES (
    v_rur_id,
    'The Revolt and the End of Humanity',
    'The robots rebel. We examine the revolution, its causes, and what Čapek is saying about the relationship between creators and created.',
    '2026-02-26 19:00:00+00',
    75,
    'Acts II and III',
    ARRAY[
      'The robots'' revolt is triggered by Helena burning the formula for creating new robots. What does this act of destruction mean symbolically?',
      'Radius says "We were machines. But now we are robots." What distinction is he drawing?',
      'Is the play''s ending — robots discovering love — hopeful or ironic?'
    ],
    'scheduled'
  ) ON CONFLICT DO NOTHING;

  -- R.U.R. Session 3: Synthesis
  INSERT INTO public.book_club_sessions
    (book_id, title, description, scheduled_at, duration_minutes, chapter_range, discussion_questions, status)
  VALUES (
    v_rur_id,
    'R.U.R. in 2026: Robots, AI, and Us',
    'A synthesis session connecting Čapek''s 1920 vision to our present moment. What did he get right? What couldn''t he have imagined?',
    '2026-03-05 19:00:00+00',
    90,
    'Full play — synthesis discussion',
    ARRAY[
      'Čapek coined "robot" from the Czech "robota" (forced labor). How does this etymology shape our understanding of AI assistants today?',
      'R.U.R. imagines robots replacing all human labor. A century later, where are we on that trajectory?',
      'The play ends with a new Adam and Eve — robots who have learned to love. Is Čapek suggesting that consciousness inevitably emerges from sufficient complexity?',
      'How does R.U.R. compare to modern AI narratives (Ex Machina, Westworld, etc.)? What has changed in how we imagine artificial beings?'
    ],
    'scheduled'
  ) ON CONFLICT DO NOTHING;

  -- War with the Newts — Session 1 (upcoming, further out)
  IF v_newts_id IS NOT NULL THEN
    INSERT INTO public.book_club_sessions
      (book_id, title, description, scheduled_at, duration_minutes, chapter_range, discussion_questions, status)
    VALUES (
      v_newts_id,
      'Discovery and Exploitation',
      'The opening of War with the Newts: Captain van Toch discovers the Newts, and humanity begins to exploit them. A session on colonialism, capitalism, and first contact.',
      '2026-03-19 19:00:00+00',
      75,
      'Book One: Andrias Scheuchzeri',
      ARRAY[
        'How does Čapek use satire differently in War with the Newts compared to R.U.R.?',
        'The Newts are initially treated as curiosities, then as labor. What real-world parallels is Čapek drawing?',
        'Captain van Toch is both sympathetic and exploitative. How does Čapek complicate our moral judgments?'
      ],
      'scheduled'
    ) ON CONFLICT DO NOTHING;
  END IF;
END $$;
