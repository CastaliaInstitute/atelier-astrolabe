-- Maieutic Tutoring and Examinations
-- Tracks Socratic dialogue sessions, tutoring, and examinations for Gate 1

-- ============================================================================
-- MAIEUTIC TUTORING SESSIONS
-- ============================================================================

-- Types of maieutic interactions
CREATE TYPE maieutic_session_type AS ENUM (
  'tutoring',        -- One-on-one or small group tutoring
  'examination',     -- Formal examination/dialogue
  'seminar',         -- Group seminar discussion
  'office_hours',    -- Faculty office hours
  'peer_dialogue'    -- Peer-to-peer maieutic dialogue
);

-- Session status
CREATE TYPE maieutic_session_status AS ENUM (
  'scheduled',       -- Scheduled but not yet started
  'in_progress',    -- Currently active
  'completed',      -- Completed successfully
  'cancelled',      -- Cancelled before completion
  'no_show'         -- Student didn't attend
);

-- Maieutic tutoring sessions
-- Note: faculty_slug FK is added conditionally below to handle cases
-- where faculty table or slug column might not exist
CREATE TABLE IF NOT EXISTS public.maieutic_sessions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  course_id text NOT NULL REFERENCES public.courses(id) ON DELETE CASCADE,
  user_id uuid NOT NULL REFERENCES public.profiles(id) ON DELETE CASCADE,
  faculty_slug text, -- Will add FK constraint conditionally below
  session_type maieutic_session_type NOT NULL DEFAULT 'tutoring',
  status maieutic_session_status NOT NULL DEFAULT 'scheduled',
  title text, -- Optional title for the session
  scheduled_at timestamptz, -- When the session is/was scheduled
  started_at timestamptz, -- When the session actually started
  completed_at timestamptz, -- When the session was completed
  duration_minutes integer, -- Actual duration in minutes
  topics_discussed text[], -- Topics covered in the session
  key_insights text, -- Key insights or breakthroughs from the session
  student_questions text[], -- Questions the student asked
  faculty_questions text[], -- Questions the faculty asked (Socratic method)
  misconceptions_identified text[], -- Misconceptions that were identified and addressed
  follow_up_required boolean DEFAULT false,
  follow_up_notes text,
  mattermost_thread_id text, -- Link to Mattermost thread if applicable
  recording_url text, -- Optional recording of the session
  notes text, -- General notes about the session
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Maieutic dialogue turns (for detailed tracking)
-- Note: speaker_faculty_slug FK is added conditionally below
CREATE TABLE IF NOT EXISTS public.maieutic_dialogue_turns (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  session_id uuid NOT NULL REFERENCES public.maieutic_sessions(id) ON DELETE CASCADE,
  turn_number integer NOT NULL, -- Order of turns in the dialogue
  speaker_id uuid REFERENCES public.profiles(id) ON DELETE SET NULL, -- User or faculty
  speaker_faculty_slug text, -- Will add FK constraint conditionally below
  is_faculty boolean DEFAULT false, -- True if faculty, false if student
  content text NOT NULL, -- The dialogue content
  question_type text, -- 'socratic', 'clarification', 'challenge', 'synthesis', etc.
  response_quality text, -- 'excellent', 'good', 'needs_work', 'misconception'
  timestamp timestamptz DEFAULT now(),
  created_at timestamptz DEFAULT now()
);

-- Maieutic examinations (formal assessment dialogues)
-- Note: examiner_faculty_slug FK is added conditionally below
CREATE TABLE IF NOT EXISTS public.maieutic_examinations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  course_id text NOT NULL REFERENCES public.courses(id) ON DELETE CASCADE,
  user_id uuid NOT NULL REFERENCES public.profiles(id) ON DELETE CASCADE,
  examiner_faculty_slug text, -- Will add FK constraint conditionally below
  examination_date timestamptz NOT NULL,
  duration_minutes integer,
  topics_examined text[] NOT NULL, -- Topics covered in the examination
  examination_format text, -- 'oral', 'written_dialogue', 'practical', 'mixed'
  questions_asked text[] NOT NULL, -- Questions asked during examination
  student_responses jsonb, -- Structured responses from the student
  examiner_assessment jsonb, -- Examiner's assessment
  fluency_score integer CHECK (fluency_score >= 0 AND fluency_score <= 100), -- 0-100
  understanding_score integer CHECK (understanding_score >= 0 AND understanding_score <= 100),
  critical_thinking_score integer CHECK (critical_thinking_score >= 0 AND critical_thinking_score <= 100),
  overall_score integer CHECK (overall_score >= 0 AND overall_score <= 100),
  passed boolean, -- Whether the examination was passed
  examiner_notes text,
  student_reflection text, -- Student's reflection on the examination
  mattermost_thread_id text,
  recording_url text,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  UNIQUE(user_id, course_id, examination_date) -- One examination per student per course per date
);

-- Add foreign key constraints to faculty.slug if faculty table and slug column exist
DO $$
BEGIN
  -- Add FK for maieutic_sessions.faculty_slug
  IF EXISTS (
    SELECT 1 FROM information_schema.tables 
    WHERE table_schema = 'public' 
    AND table_name = 'faculty'
  ) AND EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_schema = 'public' 
    AND table_name = 'faculty' 
    AND column_name = 'slug'
  ) THEN
    -- Add foreign key constraint if it doesn't exist
    IF NOT EXISTS (
      SELECT 1 FROM information_schema.table_constraints 
      WHERE constraint_schema = 'public' 
      AND table_name = 'maieutic_sessions' 
      AND constraint_name = 'maieutic_sessions_faculty_slug_fkey'
    ) THEN
      ALTER TABLE public.maieutic_sessions
      ADD CONSTRAINT maieutic_sessions_faculty_slug_fkey
      FOREIGN KEY (faculty_slug) 
      REFERENCES public.faculty(slug) 
      ON DELETE SET NULL;
    END IF;
    
    -- Add FK for maieutic_dialogue_turns.speaker_faculty_slug
    IF NOT EXISTS (
      SELECT 1 FROM information_schema.table_constraints 
      WHERE constraint_schema = 'public' 
      AND table_name = 'maieutic_dialogue_turns' 
      AND constraint_name = 'maieutic_dialogue_turns_speaker_faculty_slug_fkey'
    ) THEN
      ALTER TABLE public.maieutic_dialogue_turns
      ADD CONSTRAINT maieutic_dialogue_turns_speaker_faculty_slug_fkey
      FOREIGN KEY (speaker_faculty_slug) 
      REFERENCES public.faculty(slug) 
      ON DELETE SET NULL;
    END IF;
    
    -- Add FK for maieutic_examinations.examiner_faculty_slug
    IF NOT EXISTS (
      SELECT 1 FROM information_schema.table_constraints 
      WHERE constraint_schema = 'public' 
      AND table_name = 'maieutic_examinations' 
      AND constraint_name = 'maieutic_examinations_examiner_faculty_slug_fkey'
    ) THEN
      ALTER TABLE public.maieutic_examinations
      ADD CONSTRAINT maieutic_examinations_examiner_faculty_slug_fkey
      FOREIGN KEY (examiner_faculty_slug) 
      REFERENCES public.faculty(slug) 
      ON DELETE SET NULL;
    END IF;
  END IF;
END $$;

-- ============================================================================
-- INDEXES
-- ============================================================================

-- Maieutic sessions
CREATE INDEX IF NOT EXISTS idx_maieutic_sessions_user_course 
  ON public.maieutic_sessions(user_id, course_id);

CREATE INDEX IF NOT EXISTS idx_maieutic_sessions_faculty 
  ON public.maieutic_sessions(faculty_slug) 
  WHERE faculty_slug IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_maieutic_sessions_status 
  ON public.maieutic_sessions(status);

CREATE INDEX IF NOT EXISTS idx_maieutic_sessions_scheduled 
  ON public.maieutic_sessions(scheduled_at) 
  WHERE scheduled_at IS NOT NULL;

-- Dialogue turns
CREATE INDEX IF NOT EXISTS idx_maieutic_dialogue_turns_session 
  ON public.maieutic_dialogue_turns(session_id, turn_number);

-- Examinations
CREATE INDEX IF NOT EXISTS idx_maieutic_examinations_user_course 
  ON public.maieutic_examinations(user_id, course_id);

CREATE INDEX IF NOT EXISTS idx_maieutic_examinations_examiner 
  ON public.maieutic_examinations(examiner_faculty_slug) 
  WHERE examiner_faculty_slug IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_maieutic_examinations_date 
  ON public.maieutic_examinations(examination_date);

-- ============================================================================
-- FUNCTIONS
-- ============================================================================

-- Calculate maieutic engagement score for a user in a course
CREATE OR REPLACE FUNCTION public.calculate_maieutic_engagement(
  user_uuid uuid,
  course_id_param text
)
RETURNS jsonb AS $$
DECLARE
  session_count integer;
  examination_count integer;
  total_dialogue_turns integer;
  recent_sessions integer;
  engagement_score numeric;
  result jsonb;
BEGIN
  -- Count total sessions
  SELECT COUNT(*)
  INTO session_count
  FROM public.maieutic_sessions
  WHERE user_id = user_uuid
    AND course_id = course_id_param
    AND status = 'completed';
  
  -- Count examinations
  SELECT COUNT(*)
  INTO examination_count
  FROM public.maieutic_examinations
  WHERE user_id = user_uuid
    AND course_id = course_id_param;
  
  -- Count total dialogue turns
  SELECT COUNT(*)
  INTO total_dialogue_turns
  FROM public.maieutic_dialogue_turns dt
  JOIN public.maieutic_sessions s ON s.id = dt.session_id
  WHERE s.user_id = user_uuid
    AND s.course_id = course_id_param;
  
  -- Count recent sessions (last 30 days)
  SELECT COUNT(*)
  INTO recent_sessions
  FROM public.maieutic_sessions
  WHERE user_id = user_uuid
    AND course_id = course_id_param
    AND status = 'completed'
    AND completed_at >= now() - interval '30 days';
  
  -- Calculate engagement score (0-100)
  -- Based on: sessions (40%), examinations (30%), dialogue turns (20%), recency (10%)
  engagement_score := LEAST(100, 
    (session_count * 10) + 
    (examination_count * 15) + 
    (total_dialogue_turns / 10) + 
    (recent_sessions * 5)
  );
  
  result := jsonb_build_object(
    'session_count', session_count,
    'examination_count', examination_count,
    'total_dialogue_turns', total_dialogue_turns,
    'recent_sessions', recent_sessions,
    'engagement_score', engagement_score
  );
  
  RETURN result;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Check if user has sufficient maieutic engagement for Gate 1
CREATE OR REPLACE FUNCTION public.has_sufficient_maieutic_engagement(
  user_uuid uuid,
  course_id_param text,
  min_sessions integer DEFAULT 3,
  min_examinations integer DEFAULT 1
)
RETURNS boolean AS $$
DECLARE
  session_count integer;
  examination_count integer;
BEGIN
  SELECT COUNT(*)
  INTO session_count
  FROM public.maieutic_sessions
  WHERE user_id = user_uuid
    AND course_id = course_id_param
    AND status = 'completed';
  
  SELECT COUNT(*)
  INTO examination_count
  FROM public.maieutic_examinations
  WHERE user_id = user_uuid
    AND course_id = course_id_param;
  
  RETURN session_count >= min_sessions AND examination_count >= min_examinations;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION public.calculate_maieutic_engagement(uuid, text) TO anon, authenticated;
GRANT EXECUTE ON FUNCTION public.has_sufficient_maieutic_engagement(uuid, text, integer, integer) TO anon, authenticated;

-- ============================================================================
-- ROW LEVEL SECURITY
-- ============================================================================

-- Enable RLS
ALTER TABLE public.maieutic_sessions ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.maieutic_dialogue_turns ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.maieutic_examinations ENABLE ROW LEVEL SECURITY;

-- Maieutic sessions: Users can view their own, faculty can view all
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_sessions' 
    AND policyname = 'Users can view own sessions'
  ) THEN
    CREATE POLICY "Users can view own sessions"
      ON public.maieutic_sessions FOR SELECT
      USING (auth.uid() = user_id);
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_sessions' 
    AND policyname = 'Faculty/admin can view all sessions'
  ) THEN
    CREATE POLICY "Faculty/admin can view all sessions"
      ON public.maieutic_sessions FOR SELECT
      TO authenticated
      USING (
        EXISTS (
          SELECT 1 FROM public.profiles
          WHERE id = auth.uid()
          AND ('faculty' = ANY(roles) OR 'admin' = ANY(roles))
        )
      );
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_sessions' 
    AND policyname = 'Users can create own sessions'
  ) THEN
    CREATE POLICY "Users can create own sessions"
      ON public.maieutic_sessions FOR INSERT
      WITH CHECK (auth.uid() = user_id);
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_sessions' 
    AND policyname = 'Faculty can update sessions'
  ) THEN
    CREATE POLICY "Faculty can update sessions"
      ON public.maieutic_sessions FOR UPDATE
      TO authenticated
      USING (
        EXISTS (
          SELECT 1 FROM public.profiles
          WHERE id = auth.uid()
          AND ('faculty' = ANY(roles) OR 'admin' = ANY(roles))
        )
      );
  END IF;
END $$;

-- Dialogue turns: Same as sessions
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_dialogue_turns' 
    AND policyname = 'Users can view own dialogue turns'
  ) THEN
    CREATE POLICY "Users can view own dialogue turns"
      ON public.maieutic_dialogue_turns FOR SELECT
      USING (
        EXISTS (
          SELECT 1 FROM public.maieutic_sessions
          WHERE id = session_id
          AND user_id = auth.uid()
        )
      );
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_dialogue_turns' 
    AND policyname = 'Faculty/admin can view all dialogue turns'
  ) THEN
    CREATE POLICY "Faculty/admin can view all dialogue turns"
      ON public.maieutic_dialogue_turns FOR SELECT
      TO authenticated
      USING (
        EXISTS (
          SELECT 1 FROM public.profiles
          WHERE id = auth.uid()
          AND ('faculty' = ANY(roles) OR 'admin' = ANY(roles))
        )
      );
  END IF;
END $$;

-- Examinations: Same as sessions
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_examinations' 
    AND policyname = 'Users can view own examinations'
  ) THEN
    CREATE POLICY "Users can view own examinations"
      ON public.maieutic_examinations FOR SELECT
      USING (auth.uid() = user_id);
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_examinations' 
    AND policyname = 'Faculty/admin can view all examinations'
  ) THEN
    CREATE POLICY "Faculty/admin can view all examinations"
      ON public.maieutic_examinations FOR SELECT
      TO authenticated
      USING (
        EXISTS (
          SELECT 1 FROM public.profiles
          WHERE id = auth.uid()
          AND ('faculty' = ANY(roles) OR 'admin' = ANY(roles))
        )
      );
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'maieutic_examinations' 
    AND policyname = 'Faculty can create and update examinations'
  ) THEN
    CREATE POLICY "Faculty can create and update examinations"
      ON public.maieutic_examinations FOR ALL
      TO authenticated
      USING (
        EXISTS (
          SELECT 1 FROM public.profiles
          WHERE id = auth.uid()
          AND ('faculty' = ANY(roles) OR 'admin' = ANY(roles))
        )
      );
  END IF;
END $$;

-- ============================================================================
-- COMMENTS
-- ============================================================================

COMMENT ON TABLE public.maieutic_sessions IS 'Maieutic (Socratic) tutoring and dialogue sessions for Gate 1';
COMMENT ON TABLE public.maieutic_dialogue_turns IS 'Individual turns in maieutic dialogue sessions';
COMMENT ON TABLE public.maieutic_examinations IS 'Formal maieutic examinations for course assessment';

COMMENT ON FUNCTION public.calculate_maieutic_engagement IS 'Calculates maieutic engagement score based on sessions, examinations, and dialogue turns';
COMMENT ON FUNCTION public.has_sufficient_maieutic_engagement IS 'Checks if user has sufficient maieutic engagement for Gate 1 completion';
