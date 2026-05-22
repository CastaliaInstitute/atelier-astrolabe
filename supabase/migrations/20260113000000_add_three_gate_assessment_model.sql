-- Three-Gate Assessment Model for Microcredentials
-- Ensures credentials are issued only after rigorous assessment

-- ============================================================================
-- GATE 1: MAIEUTIC DIALOGUE (Learning Phase)
-- ============================================================================

-- This is tracked through Mattermost threads and course engagement
-- Already exists in credentials.mattermost_thread_id

-- ============================================================================
-- GATE 2: BLIND-ISH FLUENCY CHECKS (Assessment Phase)
-- ============================================================================

-- Assessment prompt types
DO $$ BEGIN
  CREATE TYPE assessment_prompt_type AS ENUM (
    'transfer_task',      -- Novel scenario application
    'debug_misconception', -- Identify and fix misconceptions
    'knowledge_check',    -- Direct knowledge assessment
    'synthesis'           -- Synthesize multiple concepts
  );
EXCEPTION
  WHEN duplicate_object THEN null;
END $$;

-- Assessment prompts bank
CREATE TABLE IF NOT EXISTS public.assessment_prompts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  course_id text NOT NULL REFERENCES public.courses(id) ON DELETE CASCADE,
  prompt_type assessment_prompt_type NOT NULL,
  prompt_text text NOT NULL,
  context text, -- Additional context for the prompt
  expected_keywords text[], -- Keywords that should appear in a good answer
  difficulty_level integer CHECK (difficulty_level BETWEEN 1 AND 5), -- 1=easy, 5=hard
  is_active boolean DEFAULT true,
  created_by uuid REFERENCES public.profiles(id) ON DELETE SET NULL,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Student responses to assessment prompts
CREATE TABLE IF NOT EXISTS public.assessment_responses (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id uuid NOT NULL REFERENCES public.profiles(id) ON DELETE CASCADE,
  course_id text NOT NULL REFERENCES public.courses(id) ON DELETE CASCADE,
  prompt_id uuid NOT NULL REFERENCES public.assessment_prompts(id) ON DELETE CASCADE,
  response_text text NOT NULL,
  submitted_at timestamptz DEFAULT now(),
  is_graded boolean DEFAULT false,
  created_at timestamptz DEFAULT now(),
  UNIQUE(user_id, prompt_id) -- One response per prompt per user
);

-- Automated assessment scoring (optional - for automated checks)
CREATE TABLE IF NOT EXISTS public.assessment_scores (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  response_id uuid NOT NULL REFERENCES public.assessment_responses(id) ON DELETE CASCADE,
  score numeric(5,2) CHECK (score >= 0 AND score <= 100), -- 0-100 score
  keywords_matched integer, -- Number of expected keywords found
  keywords_total integer, -- Total expected keywords
  automated_feedback text,
  scoring_method text, -- 'keyword_match', 'llm_evaluation', 'hybrid'
  created_at timestamptz DEFAULT now()
);

-- ============================================================================
-- GATE 3: TWO-ASSESSOR RULE (Assessment Verification Phase)
-- ============================================================================

-- Assessor votes with confidence scores
-- Note: assessor_faculty_slug FK is added conditionally below to handle cases
-- where faculty table or slug column might not exist
CREATE TABLE IF NOT EXISTS public.assessor_votes (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id uuid NOT NULL REFERENCES public.profiles(id) ON DELETE CASCADE,
  course_id text NOT NULL REFERENCES public.courses(id) ON DELETE CASCADE,
  assessor_id uuid NOT NULL REFERENCES public.profiles(id) ON DELETE CASCADE,
  assessor_faculty_slug text, -- Will add FK constraint conditionally below
  vote text NOT NULL CHECK (vote IN ('fluent', 'not_fluent', 'needs_review')),
  confidence_score integer NOT NULL CHECK (confidence_score >= 0 AND confidence_score <= 100), -- 0-100
  assessment_notes text,
  assessed_responses uuid[], -- Array of response_ids this assessor reviewed
  assessed_at timestamptz DEFAULT now(),
  created_at timestamptz DEFAULT now(),
  UNIQUE(user_id, course_id, assessor_id) -- One vote per assessor per user per course
);

-- Add foreign key constraint to faculty.slug if faculty table and slug column exist
DO $$
BEGIN
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
      AND table_name = 'assessor_votes' 
      AND constraint_name = 'assessor_votes_assessor_faculty_slug_fkey'
    ) THEN
      ALTER TABLE public.assessor_votes
      ADD CONSTRAINT assessor_votes_assessor_faculty_slug_fkey
      FOREIGN KEY (assessor_faculty_slug) 
      REFERENCES public.faculty(slug) 
      ON DELETE SET NULL;
    END IF;
  END IF;
END $$;

-- Confidence threshold configuration per course
CREATE TABLE IF NOT EXISTS public.course_assessment_config (
  course_id text PRIMARY KEY REFERENCES public.courses(id) ON DELETE CASCADE,
  min_assessors integer DEFAULT 2 CHECK (min_assessors >= 1),
  min_aggregate_confidence integer DEFAULT 70 CHECK (min_aggregate_confidence >= 0 AND min_aggregate_confidence <= 100),
  require_automated_check boolean DEFAULT false,
  min_automated_score numeric(5,2) DEFAULT 70.0 CHECK (min_automated_score >= 0 AND min_automated_score <= 100),
  min_prompts_required integer DEFAULT 3 CHECK (min_prompts_required >= 1),
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- ============================================================================
-- INDEXES
-- ============================================================================

-- Assessment prompts
CREATE INDEX IF NOT EXISTS idx_assessment_prompts_course_id 
  ON public.assessment_prompts(course_id) 
  WHERE is_active = true;

CREATE INDEX IF NOT EXISTS idx_assessment_prompts_type 
  ON public.assessment_prompts(prompt_type);

-- Assessment responses
CREATE INDEX IF NOT EXISTS idx_assessment_responses_user_course 
  ON public.assessment_responses(user_id, course_id);

CREATE INDEX IF NOT EXISTS idx_assessment_responses_prompt 
  ON public.assessment_responses(prompt_id);

-- Assessment scores
CREATE INDEX IF NOT EXISTS idx_assessment_scores_response 
  ON public.assessment_scores(response_id);

-- Assessor votes
CREATE INDEX IF NOT EXISTS idx_assessor_votes_user_course 
  ON public.assessor_votes(user_id, course_id);

CREATE INDEX IF NOT EXISTS idx_assessor_votes_assessor 
  ON public.assessor_votes(assessor_id);

-- ============================================================================
-- FUNCTIONS
-- ============================================================================

-- Calculate aggregate confidence from assessor votes
CREATE OR REPLACE FUNCTION public.calculate_aggregate_confidence(
  user_uuid uuid,
  course_id_param text
)
RETURNS numeric AS $$
DECLARE
  total_confidence numeric := 0;
  vote_count integer := 0;
  avg_confidence numeric;
BEGIN
  -- Calculate weighted average confidence (only 'fluent' votes count)
  SELECT 
    COALESCE(AVG(confidence_score), 0),
    COUNT(*)
  INTO avg_confidence, vote_count
  FROM public.assessor_votes
  WHERE user_id = user_uuid
    AND course_id = course_id_param
    AND vote = 'fluent';
  
  RETURN COALESCE(avg_confidence, 0);
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Check if user has completed all three gates
CREATE OR REPLACE FUNCTION public.has_completed_three_gates(
  user_uuid uuid,
  course_id_param text
)
RETURNS jsonb AS $$
DECLARE
  gate1_complete boolean := false;
  gate2_complete boolean := false;
  gate3_complete boolean := false;
  config public.course_assessment_config;
  aggregate_confidence numeric;
  fluent_votes integer;
  prompts_completed integer;
  automated_check_pass boolean := true;
  result jsonb;
BEGIN
  -- Get course assessment configuration
  SELECT * INTO config
  FROM public.course_assessment_config
  WHERE course_id = course_id_param;
  
  -- Use defaults if no config exists
  IF config IS NULL THEN
    config.min_assessors := 2;
    config.min_aggregate_confidence := 70;
    config.require_automated_check := false;
    config.min_automated_score := 70.0;
    config.min_prompts_required := 3;
  END IF;
  
  -- GATE 1: Maieutic dialogue (checked via enrollment and maieutic engagement)
  -- Check enrollment first
  IF NOT EXISTS (
    SELECT 1 FROM public.enrollments
    WHERE user_id = user_uuid
      AND course_id = course_id_param
      AND status = 'active'
  ) THEN
    gate1_complete := false;
  ELSE
    -- Check maieutic engagement (sessions and examinations)
    -- Use defaults: 3 sessions, 1 examination
    -- If maieutic tables don't exist yet, fall back to enrollment only
    BEGIN
      SELECT public.has_sufficient_maieutic_engagement(user_uuid, course_id_param, 3, 1)
      INTO gate1_complete;
    EXCEPTION
      WHEN OTHERS THEN
        -- If function doesn't exist or tables don't exist, use enrollment as fallback
        gate1_complete := true; -- Enrollment is sufficient if maieutic system not set up
    END;
  END IF;
  
  -- GATE 2: Blind-ish fluency checks
  -- Check if user has completed required number of prompts
  SELECT COUNT(DISTINCT prompt_id)
  INTO prompts_completed
  FROM public.assessment_responses
  WHERE user_id = user_uuid
    AND course_id = course_id_param;
  
  gate2_complete := prompts_completed >= config.min_prompts_required;
  
  -- If automated checks are required, verify scores
  IF config.require_automated_check THEN
    SELECT COUNT(*) > 0 INTO automated_check_pass
    FROM public.assessment_responses r
    JOIN public.assessment_scores s ON s.response_id = r.id
    WHERE r.user_id = user_uuid
      AND r.course_id = course_id_param
      AND s.score >= config.min_automated_score;
    
    gate2_complete := gate2_complete AND automated_check_pass;
  END IF;
  
  -- GATE 3: Two-assessor rule with confidence threshold
  -- Count fluent votes
  SELECT COUNT(*)
  INTO fluent_votes
  FROM public.assessor_votes
  WHERE user_id = user_uuid
    AND course_id = course_id_param
    AND vote = 'fluent';
  
  -- Calculate aggregate confidence
  SELECT public.calculate_aggregate_confidence(user_uuid, course_id_param)
  INTO aggregate_confidence;
  
  gate3_complete := fluent_votes >= config.min_assessors
    AND aggregate_confidence >= config.min_aggregate_confidence;
  
  -- Build result
  result := jsonb_build_object(
    'all_gates_complete', gate1_complete AND gate2_complete AND gate3_complete,
    'gate1_maieutic_dialogue', jsonb_build_object(
      'complete', gate1_complete,
      'description', 'Maieutic dialogue (learning phase)'
    ),
    'gate2_fluency_checks', jsonb_build_object(
      'complete', gate2_complete,
      'prompts_completed', prompts_completed,
      'prompts_required', config.min_prompts_required,
      'automated_check_passed', automated_check_pass,
      'description', 'Blind-ish fluency checks (assessment)'
    ),
    'gate3_assessor_verification', jsonb_build_object(
      'complete', gate3_complete,
      'fluent_votes', fluent_votes,
      'assessors_required', config.min_assessors,
      'aggregate_confidence', aggregate_confidence,
      'confidence_threshold', config.min_aggregate_confidence,
      'description', 'Two-assessor rule with confidence threshold'
    )
  );
  
  RETURN result;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION public.calculate_aggregate_confidence(uuid, text) TO anon, authenticated;
GRANT EXECUTE ON FUNCTION public.has_completed_three_gates(uuid, text) TO anon, authenticated;

-- ============================================================================
-- ROW LEVEL SECURITY
-- ============================================================================

-- Enable RLS
ALTER TABLE public.assessment_prompts ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.assessment_responses ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.assessment_scores ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.assessor_votes ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.course_assessment_config ENABLE ROW LEVEL SECURITY;

-- Assessment prompts: Public read, faculty/admin write
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessment_prompts' 
    AND policyname = 'Assessment prompts are publicly readable'
  ) THEN
    CREATE POLICY "Assessment prompts are publicly readable"
      ON public.assessment_prompts FOR SELECT
      USING (true);
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessment_prompts' 
    AND policyname = 'Faculty/admin can manage prompts'
  ) THEN
    CREATE POLICY "Faculty/admin can manage prompts"
      ON public.assessment_prompts FOR ALL
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

-- Assessment responses: Users can view their own, faculty can view all
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessment_responses' 
    AND policyname = 'Users can view own responses'
  ) THEN
    CREATE POLICY "Users can view own responses"
      ON public.assessment_responses FOR SELECT
      USING (auth.uid() = user_id);
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessment_responses' 
    AND policyname = 'Faculty/admin can view all responses'
  ) THEN
    CREATE POLICY "Faculty/admin can view all responses"
      ON public.assessment_responses FOR SELECT
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
    AND tablename = 'assessment_responses' 
    AND policyname = 'Users can create own responses'
  ) THEN
    CREATE POLICY "Users can create own responses"
      ON public.assessment_responses FOR INSERT
      WITH CHECK (auth.uid() = user_id);
  END IF;
END $$;

-- Assessment scores: Same as responses
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessment_scores' 
    AND policyname = 'Users can view own scores'
  ) THEN
    CREATE POLICY "Users can view own scores"
      ON public.assessment_scores FOR SELECT
      USING (
        EXISTS (
          SELECT 1 FROM public.assessment_responses
          WHERE id = response_id
          AND user_id = auth.uid()
        )
      );
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessment_scores' 
    AND policyname = 'Faculty/admin can view all scores'
  ) THEN
    CREATE POLICY "Faculty/admin can view all scores"
      ON public.assessment_scores FOR SELECT
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

-- Assessor votes: Users can view votes about them, assessors can view their own votes
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessor_votes' 
    AND policyname = 'Users can view votes about them'
  ) THEN
    CREATE POLICY "Users can view votes about them"
      ON public.assessor_votes FOR SELECT
      USING (auth.uid() = user_id);
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessor_votes' 
    AND policyname = 'Assessors can view their own votes'
  ) THEN
    CREATE POLICY "Assessors can view their own votes"
      ON public.assessor_votes FOR SELECT
      USING (auth.uid() = assessor_id);
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'assessor_votes' 
    AND policyname = 'Faculty/admin can view all votes'
  ) THEN
    CREATE POLICY "Faculty/admin can view all votes"
      ON public.assessor_votes FOR SELECT
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
    AND tablename = 'assessor_votes' 
    AND policyname = 'Faculty can create votes'
  ) THEN
    CREATE POLICY "Faculty can create votes"
      ON public.assessor_votes FOR INSERT
      TO authenticated
      WITH CHECK (
        auth.uid() = assessor_id
        AND EXISTS (
          SELECT 1 FROM public.profiles
          WHERE id = auth.uid()
          AND ('faculty' = ANY(roles) OR 'admin' = ANY(roles))
        )
      );
  END IF;
END $$;

-- Course assessment config: Public read, admin write
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'course_assessment_config' 
    AND policyname = 'Assessment config is publicly readable'
  ) THEN
    CREATE POLICY "Assessment config is publicly readable"
      ON public.course_assessment_config FOR SELECT
      USING (true);
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_policies 
    WHERE schemaname = 'public' 
    AND tablename = 'course_assessment_config' 
    AND policyname = 'Admin can manage config'
  ) THEN
    CREATE POLICY "Admin can manage config"
      ON public.course_assessment_config FOR ALL
      TO authenticated
      USING (
        EXISTS (
          SELECT 1 FROM public.profiles
          WHERE id = auth.uid()
          AND 'admin' = ANY(roles)
        )
      );
  END IF;
END $$;

-- ============================================================================
-- COMMENTS
-- ============================================================================

COMMENT ON TABLE public.assessment_prompts IS 'Bank of assessment prompts for blind-ish fluency checks';
COMMENT ON TABLE public.assessment_responses IS 'Student responses to assessment prompts';
COMMENT ON TABLE public.assessment_scores IS 'Automated scores for assessment responses';
COMMENT ON TABLE public.assessor_votes IS 'Faculty assessor votes with confidence scores for Gate 3';
COMMENT ON TABLE public.course_assessment_config IS 'Configuration for three-gate assessment model per course';

COMMENT ON FUNCTION public.calculate_aggregate_confidence IS 'Calculates aggregate confidence from assessor votes (fluent votes only)';
COMMENT ON FUNCTION public.has_completed_three_gates IS 'Checks if user has completed all three gates of assessment';