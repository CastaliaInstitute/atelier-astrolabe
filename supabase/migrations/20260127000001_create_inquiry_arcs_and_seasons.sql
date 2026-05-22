-- ============================================================================
-- MORE HUMAN THAN HUMAN: Inquiry Arcs & Seasons Schema
-- ============================================================================
-- This migration creates the data model for multi-season inquiry arcs like
-- "More Human Than Human" - programs that span multiple cohort-based seasons
-- with cumulative learning and distinct assessment models.
-- ============================================================================

-- Create enum for season status
CREATE TYPE season_status AS ENUM (
    'announced',      -- Publicly visible but not enrolling
    'enrolling',      -- Open for enrollment
    'in_progress',    -- Currently running
    'completed',      -- Finished
    'archived'        -- No longer active
);

-- Create enum for enrollment mode
CREATE TYPE enrollment_mode AS ENUM (
    'open',           -- Anyone can enroll at any time (async)
    'cohort',         -- Cohort-based with enrollment windows
    'invitation'      -- By invitation only
);

-- Create enum for assessment outcome (MHH specific: Pass/Revise/Refuse)
CREATE TYPE assessment_outcome AS ENUM (
    'pass',           -- Demonstrates fluency and reflection
    'revise',         -- Needs additional work and re-examination
    'refuse',         -- Valid pedagogical choice - decline to complete
    'pending',        -- Not yet assessed
    'withdrawn'       -- Student withdrew
);

-- ============================================================================
-- INQUIRY ARCS TABLE
-- ============================================================================
-- An arc is a multi-season program with a unified thesis and cumulative structure
CREATE TABLE inquiry_arcs (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    slug TEXT UNIQUE NOT NULL,
    title TEXT NOT NULL,
    subtitle TEXT,
    
    -- Core question driving the entire arc
    core_question TEXT NOT NULL,
    thesis TEXT,
    
    -- Visual identity
    banner_image_url TEXT,
    icon_url TEXT,
    color_scheme JSONB DEFAULT '{}',
    
    -- Organizational
    custodian TEXT DEFAULT 'Inquiry Institute',
    lead_faculty_slug TEXT NOT NULL,  -- e.g., 'a-turing'
    
    -- Metadata
    guiding_sentence TEXT,  -- "This course is not about proving you are human..."
    
    -- Content
    pedagogical_model JSONB DEFAULT '{}',
    ethics_safeguards TEXT[],
    
    -- Credential
    credential_title TEXT,
    credential_description TEXT,
    
    -- Status
    published BOOLEAN DEFAULT false,
    created_at TIMESTAMPTZ DEFAULT now(),
    updated_at TIMESTAMPTZ DEFAULT now()
);

-- Enable RLS
ALTER TABLE inquiry_arcs ENABLE ROW LEVEL SECURITY;

-- Public can read published arcs
CREATE POLICY "Public can read published arcs" ON inquiry_arcs
    FOR SELECT USING (published = true);

-- Service role has full access
CREATE POLICY "Service role has full access to arcs" ON inquiry_arcs
    FOR ALL USING (auth.role() = 'service_role');

-- ============================================================================
-- SEASONS TABLE
-- ============================================================================
-- Each season is a distinct phase within an arc
CREATE TABLE arc_seasons (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    arc_id UUID NOT NULL REFERENCES inquiry_arcs(id) ON DELETE CASCADE,
    
    -- Identity
    season_number INTEGER NOT NULL,  -- 0, 1, 2...
    slug TEXT NOT NULL,
    title TEXT NOT NULL,
    subtitle TEXT,
    
    -- Timing
    enrollment_mode enrollment_mode NOT NULL DEFAULT 'cohort',
    enrollment_start TIMESTAMPTZ,
    enrollment_end TIMESTAMPTZ,
    run_start TIMESTAMPTZ,
    run_end TIMESTAMPTZ,
    duration_weeks INTEGER,
    
    -- Content
    description TEXT,
    core_texts TEXT[],
    content_modules JSONB DEFAULT '[]',  -- Array of module objects
    
    -- Faculty
    faculty_slugs TEXT[] DEFAULT '{}',  -- Faculty for this season
    heretic_rotation BOOLEAN DEFAULT false,
    
    -- Pricing
    price_usd NUMERIC(10, 2),
    credit_toward_next BOOLEAN DEFAULT false,  -- Price applies as credit to next season
    
    -- Assessment
    assessment_model TEXT DEFAULT 'pass_revise_refuse',
    assessment_config JSONB DEFAULT '{}',
    
    -- Technical (for cohort-based seasons)
    github_classroom_url TEXT,
    matrix_room_id TEXT,
    
    -- Status
    status season_status DEFAULT 'announced',
    enrollment_cap INTEGER,
    
    -- Timestamps
    created_at TIMESTAMPTZ DEFAULT now(),
    updated_at TIMESTAMPTZ DEFAULT now(),
    
    UNIQUE(arc_id, season_number),
    UNIQUE(arc_id, slug)
);

-- Enable RLS
ALTER TABLE arc_seasons ENABLE ROW LEVEL SECURITY;

-- Public can read seasons of published arcs
CREATE POLICY "Public can read seasons of published arcs" ON arc_seasons
    FOR SELECT USING (
        EXISTS (
            SELECT 1 FROM inquiry_arcs 
            WHERE inquiry_arcs.id = arc_seasons.arc_id 
            AND inquiry_arcs.published = true
        )
    );

-- Service role has full access
CREATE POLICY "Service role has full access to seasons" ON arc_seasons
    FOR ALL USING (auth.role() = 'service_role');

-- ============================================================================
-- SEASON ENROLLMENTS TABLE
-- ============================================================================
CREATE TABLE arc_enrollments (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    season_id UUID NOT NULL REFERENCES arc_seasons(id) ON DELETE CASCADE,
    
    -- Status
    enrolled_at TIMESTAMPTZ DEFAULT now(),
    status TEXT DEFAULT 'active' CHECK (status IN ('active', 'completed', 'withdrawn', 'deferred')),
    
    -- Assessment
    assessment_outcome assessment_outcome DEFAULT 'pending',
    assessment_notes TEXT,
    assessed_at TIMESTAMPTZ,
    assessed_by TEXT,  -- Faculty slug who performed final assessment
    
    -- Artifacts (reflective essays, field notes, etc.)
    artifacts JSONB DEFAULT '[]',
    
    -- Progression
    modules_completed TEXT[] DEFAULT '{}',
    
    -- Timestamps
    updated_at TIMESTAMPTZ DEFAULT now(),
    
    UNIQUE(user_id, season_id)
);

-- Enable RLS
ALTER TABLE arc_enrollments ENABLE ROW LEVEL SECURITY;

-- Users can read their own enrollments
CREATE POLICY "Users can read own enrollments" ON arc_enrollments
    FOR SELECT USING (auth.uid() = user_id);

-- Users can insert their own enrollments
CREATE POLICY "Users can enroll themselves" ON arc_enrollments
    FOR INSERT WITH CHECK (auth.uid() = user_id);

-- Users can update their own enrollments (limited fields)
CREATE POLICY "Users can update own enrollments" ON arc_enrollments
    FOR UPDATE USING (auth.uid() = user_id);

-- Service role has full access
CREATE POLICY "Service role has full access to enrollments" ON arc_enrollments
    FOR ALL USING (auth.role() = 'service_role');

-- ============================================================================
-- ARC FACULTY ASSIGNMENTS TABLE
-- ============================================================================
-- Maps faculty to their roles in each arc/season
CREATE TABLE arc_faculty (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    arc_id UUID NOT NULL REFERENCES inquiry_arcs(id) ON DELETE CASCADE,
    season_id UUID REFERENCES arc_seasons(id) ON DELETE CASCADE,  -- NULL means arc-wide
    faculty_slug TEXT NOT NULL,
    
    -- Role in the arc
    role TEXT NOT NULL DEFAULT 'core' CHECK (role IN ('lead', 'core', 'heretic', 'guest')),
    
    -- For heretic rotation
    symposium_number INTEGER,  -- Which symposium they're heretic for
    
    -- Additional context
    focus_area TEXT,  -- What aspect they cover
    
    created_at TIMESTAMPTZ DEFAULT now(),
    
    UNIQUE(arc_id, season_id, faculty_slug, symposium_number)
);

-- Enable RLS
ALTER TABLE arc_faculty ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Public can read arc faculty" ON arc_faculty
    FOR SELECT USING (true);

CREATE POLICY "Service role has full access to arc faculty" ON arc_faculty
    FOR ALL USING (auth.role() = 'service_role');

-- ============================================================================
-- INDEXES
-- ============================================================================
CREATE INDEX idx_arc_seasons_arc_id ON arc_seasons(arc_id);
CREATE INDEX idx_arc_seasons_status ON arc_seasons(status);
CREATE INDEX idx_arc_enrollments_user_id ON arc_enrollments(user_id);
CREATE INDEX idx_arc_enrollments_season_id ON arc_enrollments(season_id);
CREATE INDEX idx_arc_faculty_arc_id ON arc_faculty(arc_id);
CREATE INDEX idx_arc_faculty_faculty_slug ON arc_faculty(faculty_slug);

-- ============================================================================
-- TRIGGERS FOR UPDATED_AT
-- ============================================================================
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = now();
    RETURN NEW;
END;
$$ language 'plpgsql';

CREATE TRIGGER update_inquiry_arcs_updated_at
    BEFORE UPDATE ON inquiry_arcs
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_arc_seasons_updated_at
    BEFORE UPDATE ON arc_seasons
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_arc_enrollments_updated_at
    BEFORE UPDATE ON arc_enrollments
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- ============================================================================
-- COMMENTS
-- ============================================================================
COMMENT ON TABLE inquiry_arcs IS 'Multi-season inquiry programs with unified thesis (e.g., More Human Than Human)';
COMMENT ON TABLE arc_seasons IS 'Individual seasons within an inquiry arc';
COMMENT ON TABLE arc_enrollments IS 'User enrollments in arc seasons';
COMMENT ON TABLE arc_faculty IS 'Faculty assignments to arcs and seasons';
COMMENT ON COLUMN arc_enrollments.assessment_outcome IS 'MHH uses Pass/Revise/Refuse model - refuse is pedagogically valid';
