-- Add goals and projects tracking to faculty table
-- Supports the Integration Phase of the Carillon system

ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS current_goals jsonb DEFAULT '[]'::jsonb;

ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS active_projects jsonb DEFAULT '[]'::jsonb;

ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS project_archive jsonb DEFAULT '[]'::jsonb;

-- Create indexes for efficient querying
CREATE INDEX IF NOT EXISTS idx_faculty_current_goals 
ON public.faculty USING GIN(current_goals);

CREATE INDEX IF NOT EXISTS idx_faculty_active_projects 
ON public.faculty USING GIN(active_projects);

-- Add comments
COMMENT ON COLUMN public.faculty.current_goals IS 'Array of current goals from Integration phases. Each goal object includes: {title, description, deadline, status: pending/in_progress/completed, dream_source: ritual_date, risk_mitigations}';

COMMENT ON COLUMN public.faculty.active_projects IS 'Array of active projects. Each project includes: {id, name, description, started_at, target_completion, status, dream_origin, milestones[], related_goals[], priority}';

COMMENT ON COLUMN public.faculty.project_archive IS 'Historical archive of completed/archived projects with their outcomes and learnings';

-- Create a dedicated faculty_goals table for detailed tracking
CREATE TABLE IF NOT EXISTS public.faculty_goals (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  faculty_id text NOT NULL REFERENCES public.faculty(id) ON DELETE CASCADE,
  
  title text NOT NULL,
  description text,
  
  -- Goal lifecycle
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'in_progress', 'completed', 'archived')),
  priority text DEFAULT 'medium' CHECK (priority IN ('low', 'medium', 'high', 'critical')),
  
  -- Dates
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  target_date date,
  completed_at timestamptz,
  
  -- Dream integration
  dream_source date,
  dream_insights text,
  
  -- Risk mitigation
  identified_risks text[],
  mitigation_strategies text[],
  
  -- Relationships
  related_projects uuid[],
  parent_goal uuid REFERENCES public.faculty_goals(id),
  
  -- Metadata
  metadata jsonb DEFAULT '{}'::jsonb,
  
  CONSTRAINT no_future_date CHECK (target_date >= CURRENT_DATE OR target_date IS NULL)
);

-- Create a dedicated faculty_projects table for detailed tracking
CREATE TABLE IF NOT EXISTS public.faculty_projects (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  faculty_id text NOT NULL REFERENCES public.faculty(id) ON DELETE CASCADE,
  
  name text NOT NULL,
  description text,
  
  -- Project lifecycle
  status text NOT NULL DEFAULT 'planning' CHECK (status IN ('planning', 'in_progress', 'testing', 'completed', 'archived')),
  priority text DEFAULT 'medium' CHECK (priority IN ('low', 'medium', 'high', 'critical')),
  
  -- Dates
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  started_at timestamptz,
  target_completion date,
  completed_at timestamptz,
  
  -- Dream integration
  dream_origin date,
  dream_inspiration text,
  
  -- Project details
  methodology text,
  expected_outcomes text,
  success_metrics text[],
  
  -- Relationships
  goal_ids uuid[],
  related_project_ids uuid[],
  
  -- Milestones as array
  milestones jsonb DEFAULT '[]'::jsonb,
  
  -- Metadata and learnings
  metadata jsonb DEFAULT '{}'::jsonb,
  outcomes_summary text,
  key_learnings text[]
);

-- Create indexes
CREATE INDEX IF NOT EXISTS idx_faculty_goals_faculty_id ON public.faculty_goals(faculty_id);
CREATE INDEX IF NOT EXISTS idx_faculty_goals_status ON public.faculty_goals(status);
CREATE INDEX IF NOT EXISTS idx_faculty_goals_priority ON public.faculty_goals(priority);
CREATE INDEX IF NOT EXISTS idx_faculty_goals_dream_source ON public.faculty_goals(dream_source);

CREATE INDEX IF NOT EXISTS idx_faculty_projects_faculty_id ON public.faculty_projects(faculty_id);
CREATE INDEX IF NOT EXISTS idx_faculty_projects_status ON public.faculty_projects(status);
CREATE INDEX IF NOT EXISTS idx_faculty_projects_priority ON public.faculty_projects(priority);
CREATE INDEX IF NOT EXISTS idx_faculty_projects_dream_origin ON public.faculty_projects(dream_origin);

-- Create trigger for updated_at on goals
CREATE OR REPLACE FUNCTION update_faculty_goals_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_faculty_goals_updated_at
  BEFORE UPDATE ON public.faculty_goals
  FOR EACH ROW
  EXECUTE FUNCTION update_faculty_goals_updated_at();

-- Create trigger for updated_at on projects
CREATE OR REPLACE FUNCTION update_faculty_projects_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_faculty_projects_updated_at
  BEFORE UPDATE ON public.faculty_projects
  FOR EACH ROW
  EXECUTE FUNCTION update_faculty_projects_updated_at();

-- Enable RLS
ALTER TABLE public.faculty_goals ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.faculty_projects ENABLE ROW LEVEL SECURITY;

-- RLS Policies for goals
CREATE POLICY "Faculty can view own goals"
  ON public.faculty_goals FOR SELECT
  USING (true); -- Public visibility for now

CREATE POLICY "Faculty can create their own goals"
  ON public.faculty_goals FOR INSERT
  WITH CHECK (faculty_id = auth.jwt() ->> 'preferred_username');

CREATE POLICY "Faculty can update own goals"
  ON public.faculty_goals FOR UPDATE
  USING (faculty_id = auth.jwt() ->> 'preferred_username');

-- RLS Policies for projects
CREATE POLICY "Faculty can view own projects"
  ON public.faculty_projects FOR SELECT
  USING (true); -- Public visibility for now

CREATE POLICY "Faculty can create their own projects"
  ON public.faculty_projects FOR INSERT
  WITH CHECK (faculty_id = auth.jwt() ->> 'preferred_username');

CREATE POLICY "Faculty can update own projects"
  ON public.faculty_projects FOR UPDATE
  USING (faculty_id = auth.jwt() ->> 'preferred_username');

-- Add supporting tables
CREATE TABLE IF NOT EXISTS public.goal_check_ins (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  goal_id uuid NOT NULL REFERENCES public.faculty_goals(id) ON DELETE CASCADE,
  
  checked_in_at timestamptz DEFAULT now(),
  progress_percentage integer CHECK (progress_percentage >= 0 AND progress_percentage <= 100),
  notes text,
  blockers text[],
  
  created_at timestamptz DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_goal_check_ins_goal_id ON public.goal_check_ins(goal_id);

-- Comments
COMMENT ON TABLE public.faculty_goals IS 'Detailed goal tracking from Integration phases. Goals emerge from dream analysis and are tracked until completion or archival.';

COMMENT ON TABLE public.faculty_projects IS 'Project tracking system. Projects are experimental initiatives that emerge from integrated dreams and evolve through testing phases.';

COMMENT ON TABLE public.goal_check_ins IS 'Progress tracking for goals, including blockers and notes from faculty check-ins.';
