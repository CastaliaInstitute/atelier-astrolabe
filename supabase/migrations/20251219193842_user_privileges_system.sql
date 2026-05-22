-- User Privileges System
-- Links user memberships to privileges/permissions

-- ============================================================================
-- PRIVILEGES TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.privileges (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name text UNIQUE NOT NULL, -- e.g., 'alpha', 'beta', 'admin', 'faculty-club'
  description text,
  level integer DEFAULT 0, -- Higher number = more privileges (for comparison)
  metadata jsonb, -- Additional privilege configuration
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_privileges_name ON public.privileges(name);
CREATE INDEX IF NOT EXISTS idx_privileges_level ON public.privileges(level);

-- ============================================================================
-- USER PRIVILEGES TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.user_privileges (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id uuid, -- Optional reference (no FK to avoid profiles dependency)
  email text NOT NULL, -- Denormalized for easier lookup without user_id
  privilege_id uuid REFERENCES public.privileges(id) ON DELETE CASCADE,
  privilege_name text NOT NULL, -- Denormalized for easier lookup
  source text NOT NULL DEFAULT 'membership', -- 'membership', 'manual', 'system'
  source_id text, -- e.g., membership_id, admin_user_id
  granted_at timestamptz DEFAULT now(),
  expires_at timestamptz, -- NULL = never expires
  is_active boolean DEFAULT true,
  metadata jsonb, -- Additional context (plan_id, tier, etc.)
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  UNIQUE(user_id, privilege_id),
  UNIQUE(email, privilege_id)
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_user_privileges_user_id ON public.user_privileges(user_id);
CREATE INDEX IF NOT EXISTS idx_user_privileges_email ON public.user_privileges(email);
CREATE INDEX IF NOT EXISTS idx_user_privileges_privilege_id ON public.user_privileges(privilege_id);
CREATE INDEX IF NOT EXISTS idx_user_privileges_privilege_name ON public.user_privileges(privilege_name);
CREATE INDEX IF NOT EXISTS idx_user_privileges_active ON public.user_privileges(is_active) WHERE is_active = true;
CREATE INDEX IF NOT EXISTS idx_user_privileges_source ON public.user_privileges(source);

-- ============================================================================
-- PRIVILEGE TO MEMBERSHIP PLAN MAPPING
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.privilege_plan_mapping (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  privilege_name text NOT NULL REFERENCES public.privileges(name) ON DELETE CASCADE,
  plan_id text NOT NULL, -- 'alpha', 'monthly', 'annual', 'midwinter'
  is_active boolean DEFAULT true,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  UNIQUE(privilege_name, plan_id)
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_privilege_plan_mapping_privilege ON public.privilege_plan_mapping(privilege_name);
CREATE INDEX IF NOT EXISTS idx_privilege_plan_mapping_plan ON public.privilege_plan_mapping(plan_id);

-- ============================================================================
-- FUNCTIONS
-- ============================================================================

-- Function to update updated_at timestamp
CREATE OR REPLACE FUNCTION update_privileges_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Triggers for updated_at
CREATE TRIGGER update_privileges_updated_at
  BEFORE UPDATE ON public.privileges
  FOR EACH ROW
  EXECUTE FUNCTION update_privileges_updated_at();

CREATE TRIGGER update_user_privileges_updated_at
  BEFORE UPDATE ON public.user_privileges
  FOR EACH ROW
  EXECUTE FUNCTION update_privileges_updated_at();

CREATE TRIGGER update_privilege_plan_mapping_updated_at
  BEFORE UPDATE ON public.privilege_plan_mapping
  FOR EACH ROW
  EXECUTE FUNCTION update_privileges_updated_at();

-- Function to grant privilege to user
CREATE OR REPLACE FUNCTION grant_privilege_to_user(
  p_email text,
  p_privilege_name text,
  p_source text DEFAULT 'manual',
  p_source_id text DEFAULT NULL,
  p_expires_at timestamptz DEFAULT NULL,
  p_metadata jsonb DEFAULT NULL
)
RETURNS uuid AS $$
DECLARE
  v_privilege_id uuid;
  v_user_id uuid;
  v_privilege_user_id uuid;
BEGIN
  -- Get privilege ID
  SELECT id INTO v_privilege_id
  FROM public.privileges
  WHERE name = p_privilege_name;

  IF v_privilege_id IS NULL THEN
    RAISE EXCEPTION 'Privilege % does not exist', p_privilege_name;
  END IF;

  -- Get user ID if exists (optional) - commented until profiles exists
  -- SELECT id INTO v_user_id
  -- FROM public.profiles
  -- WHERE email = p_email
  -- LIMIT 1;
  v_user_id := NULL; -- Set to NULL until profiles table exists

  -- Insert or update user privilege
  INSERT INTO public.user_privileges (
    user_id,
    email,
    privilege_id,
    privilege_name,
    source,
    source_id,
    expires_at,
    metadata,
    is_active
  )
  VALUES (
    v_user_id,
    p_email,
    v_privilege_id,
    p_privilege_name,
    p_source,
    p_source_id,
    p_expires_at,
    p_metadata,
    true
  )
  ON CONFLICT (email, privilege_id)
  DO UPDATE SET
    is_active = true,
    expires_at = p_expires_at,
    metadata = COALESCE(p_metadata, user_privileges.metadata),
    updated_at = now()
  RETURNING id INTO v_privilege_user_id;

  RETURN v_privilege_user_id;
END;
$$ LANGUAGE plpgsql;

-- Function to revoke privilege from user
CREATE OR REPLACE FUNCTION revoke_privilege_from_user(
  p_email text,
  p_privilege_name text
)
RETURNS boolean AS $$
BEGIN
  UPDATE public.user_privileges
  SET is_active = false,
      updated_at = now()
  WHERE email = p_email
    AND privilege_name = p_privilege_name
    AND is_active = true;

  RETURN FOUND;
END;
$$ LANGUAGE plpgsql;

-- Function to check if user has privilege
CREATE OR REPLACE FUNCTION user_has_privilege(
  p_email text,
  p_privilege_name text
)
RETURNS boolean AS $$
DECLARE
  v_has_privilege boolean;
BEGIN
  SELECT EXISTS(
    SELECT 1
    FROM public.user_privileges
    WHERE email = p_email
      AND privilege_name = p_privilege_name
      AND is_active = true
      AND (expires_at IS NULL OR expires_at > now())
  ) INTO v_has_privilege;

  RETURN v_has_privilege;
END;
$$ LANGUAGE plpgsql;

-- Function to sync privileges from membership
CREATE OR REPLACE FUNCTION sync_privileges_from_membership()
RETURNS TRIGGER AS $$
DECLARE
  v_privilege_name text;
BEGIN
  -- Only process active memberships
  IF NEW.status = 'active' AND (NEW.current_period_end IS NULL OR NEW.current_period_end > now()) THEN
    -- Get privileges for this plan
    FOR v_privilege_name IN
      SELECT privilege_name
      FROM public.privilege_plan_mapping
      WHERE plan_id = NEW.plan_id
        AND is_active = true
    LOOP
      -- Grant privilege
      PERFORM grant_privilege_to_user(
        NEW.email,
        v_privilege_name,
        'membership',
        NEW.id::text,
        NEW.current_period_end,
        jsonb_build_object('plan_id', NEW.plan_id, 'membership_id', NEW.id)
      );
    END LOOP;
  ELSE
    -- Revoke privileges if membership is not active
    FOR v_privilege_name IN
      SELECT privilege_name
      FROM public.privilege_plan_mapping
      WHERE plan_id = NEW.plan_id
    LOOP
      PERFORM revoke_privilege_from_user(NEW.email, v_privilege_name);
    END LOOP;
  END IF;

  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Trigger to sync privileges when membership changes
CREATE TRIGGER sync_privileges_on_membership_change
  AFTER INSERT OR UPDATE OF status, plan_id, current_period_end ON public.memberships
  FOR EACH ROW
  EXECUTE FUNCTION sync_privileges_from_membership();

-- ============================================================================
-- INITIAL DATA
-- ============================================================================

-- Create 'alpha' privilege
INSERT INTO public.privileges (name, description, level, metadata)
VALUES (
  'alpha',
  'Alpha membership privileges - early access and special features',
  10,
  '{"features": ["early_access", "alpha_features", "priority_support"]}'::jsonb
)
ON CONFLICT (name) DO NOTHING;

-- Map alpha plan to alpha privilege
INSERT INTO public.privilege_plan_mapping (privilege_name, plan_id, is_active)
VALUES ('alpha', 'alpha', true)
ON CONFLICT (privilege_name, plan_id) DO UPDATE SET is_active = true;

-- ============================================================================
-- ROW LEVEL SECURITY (RLS)
-- ============================================================================

-- Enable RLS
ALTER TABLE public.privileges ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.user_privileges ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.privilege_plan_mapping ENABLE ROW LEVEL SECURITY;

-- Public read access to privileges (for transparency)
CREATE POLICY "Public can view privileges"
  ON public.privileges
  FOR SELECT
  TO anon, authenticated
  USING (true);

-- Users can view their own privileges (simplified until profiles exists)
CREATE POLICY "Users can view own privileges"
  ON public.user_privileges
  FOR SELECT
  TO anon, authenticated
  USING (
    email = (SELECT raw_user_meta_data->>'email' FROM auth.users WHERE id = auth.uid())
  );

-- Service role full access
CREATE POLICY "Service role full access to privileges"
  ON public.privileges
  FOR ALL
  USING (auth.role() = 'service_role');

CREATE POLICY "Service role full access to user privileges"
  ON public.user_privileges
  FOR ALL
  USING (auth.role() = 'service_role');

CREATE POLICY "Service role full access to privilege plan mapping"
  ON public.privilege_plan_mapping
  FOR ALL
  USING (auth.role() = 'service_role');

-- ============================================================================
-- COMMENTS
-- ============================================================================

COMMENT ON TABLE public.privileges IS 'Defines available privileges/permissions in the system';
COMMENT ON TABLE public.user_privileges IS 'Tracks which users have which privileges and when they expire';
COMMENT ON TABLE public.privilege_plan_mapping IS 'Maps membership plans to privileges (e.g., alpha plan -> alpha privilege)';
COMMENT ON FUNCTION grant_privilege_to_user IS 'Grants a privilege to a user by email';
COMMENT ON FUNCTION revoke_privilege_from_user IS 'Revokes a privilege from a user';
COMMENT ON FUNCTION user_has_privilege IS 'Checks if a user has an active privilege';
COMMENT ON FUNCTION sync_privileges_from_membership IS 'Automatically syncs privileges when membership status changes';
