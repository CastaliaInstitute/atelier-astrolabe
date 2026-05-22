-- ============================================================================
-- ENTITLEMENTS SYSTEM
-- ============================================================================
-- The entitlements table is the heart of access control. Everything downstream
-- (courses, symposia, salons, Matrix rooms, archives) checks entitlements.
-- 
-- Payments CREATE entitlements. Content CHECKS entitlements.
-- This decouples "who paid" from "who can access".
-- ============================================================================

-- ============================================================================
-- ENTITLEMENTS TABLE
-- ============================================================================
CREATE TABLE public.entitlements (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    
    -- Who has the entitlement
    user_id UUID REFERENCES auth.users(id) ON DELETE CASCADE,
    email TEXT NOT NULL,  -- Denormalized for lookups before user exists
    
    -- What they have access to
    resource_type TEXT NOT NULL CHECK (resource_type IN (
        'arc',           -- Full inquiry arc access
        'season',        -- Individual season access
        'symposium',     -- Single symposium access
        'salon',         -- Salon access
        'archive',       -- Archive access
        'membership',    -- General membership tier
        'faculty_club',  -- Faculty Club access
        'matrix_room',   -- Specific Matrix room access
        'lecture',       -- Individual lecture access
        'credential'     -- Micro-credential access
    )),
    resource_id TEXT NOT NULL,  -- e.g., 'more-human-than-human', 'electric-sheep'
    
    -- Access level
    access_level TEXT NOT NULL DEFAULT 'participate' CHECK (access_level IN (
        'view',         -- Can view content only
        'participate',  -- Can participate in discussions, submit work
        'host',         -- Can host symposia, moderate discussions
        'admin'         -- Full administrative access
    )),
    
    -- Time bounds
    starts_at TIMESTAMPTZ DEFAULT now(),
    ends_at TIMESTAMPTZ,  -- NULL means perpetual access
    
    -- Source of entitlement (audit trail)
    source TEXT NOT NULL CHECK (source IN (
        'stripe_checkout',      -- From Stripe checkout
        'stripe_subscription',  -- From recurring subscription
        'opencollective',       -- From OpenCollective
        'admin_grant',          -- Manually granted by admin
        'faculty',              -- Faculty member (automatic)
        'comped',               -- Complimentary access
        'scholarship',          -- Scholarship grant
        'bundle',               -- Part of a bundle purchase
        'credit'                -- Credit from previous purchase
    )),
    source_id TEXT,  -- e.g., stripe checkout session ID, admin user ID
    
    -- Stripe references (when applicable)
    stripe_checkout_session_id TEXT,
    stripe_subscription_id TEXT,
    stripe_customer_id TEXT,
    
    -- Metadata
    metadata JSONB DEFAULT '{}',
    notes TEXT,  -- Admin notes
    
    -- State (computed at query time, not stored)
    -- Note: is_active is a virtual check done via function, not a generated column
    -- because now() is not immutable
    
    -- Timestamps
    created_at TIMESTAMPTZ DEFAULT now(),
    updated_at TIMESTAMPTZ DEFAULT now(),
    revoked_at TIMESTAMPTZ,  -- If access was revoked early
    revoked_by TEXT,
    revoked_reason TEXT,
    
    -- Unique constraint: one entitlement per user per resource
    UNIQUE(user_id, resource_type, resource_id)
);

-- ============================================================================
-- HELPER FUNCTION FOR is_active CHECK
-- ============================================================================
CREATE OR REPLACE FUNCTION public.entitlement_is_active(ent public.entitlements)
RETURNS BOOLEAN
LANGUAGE sql
STABLE
AS $$
    SELECT (ent.starts_at IS NULL OR ent.starts_at <= now()) 
       AND (ent.ends_at IS NULL OR ent.ends_at > now())
       AND ent.revoked_at IS NULL
$$;

-- ============================================================================
-- INDEXES
-- ============================================================================
CREATE INDEX idx_entitlements_user_id ON public.entitlements(user_id);
CREATE INDEX idx_entitlements_email ON public.entitlements(email);
CREATE INDEX idx_entitlements_resource ON public.entitlements(resource_type, resource_id);
CREATE INDEX idx_entitlements_dates ON public.entitlements(starts_at, ends_at);
CREATE INDEX idx_entitlements_stripe_session ON public.entitlements(stripe_checkout_session_id) 
    WHERE stripe_checkout_session_id IS NOT NULL;
CREATE INDEX idx_entitlements_stripe_subscription ON public.entitlements(stripe_subscription_id) 
    WHERE stripe_subscription_id IS NOT NULL;

-- ============================================================================
-- ROW LEVEL SECURITY
-- ============================================================================
ALTER TABLE public.entitlements ENABLE ROW LEVEL SECURITY;

-- Users can read their own entitlements
CREATE POLICY "Users can read own entitlements" ON public.entitlements
    FOR SELECT USING (auth.uid() = user_id);

-- Service role has full access
CREATE POLICY "Service role full access to entitlements" ON public.entitlements
    FOR ALL USING (auth.role() = 'service_role');

-- ============================================================================
-- TRIGGER FOR UPDATED_AT
-- ============================================================================
CREATE TRIGGER update_entitlements_updated_at
    BEFORE UPDATE ON public.entitlements
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- ============================================================================
-- HELPER FUNCTIONS
-- ============================================================================

-- Check if user has active entitlement for a resource
CREATE OR REPLACE FUNCTION public.has_entitlement(
    p_user_id UUID,
    p_resource_type TEXT,
    p_resource_id TEXT,
    p_min_access_level TEXT DEFAULT 'view'
)
RETURNS BOOLEAN
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_access_levels TEXT[] := ARRAY['view', 'participate', 'host', 'admin'];
    v_min_level_idx INT;
    v_has_access BOOLEAN;
BEGIN
    -- Get minimum required level index
    v_min_level_idx := array_position(v_access_levels, p_min_access_level);
    
    SELECT EXISTS (
        SELECT 1 FROM public.entitlements e
        WHERE e.user_id = p_user_id
        AND e.resource_type = p_resource_type
        AND e.resource_id = p_resource_id
        AND e.revoked_at IS NULL
        AND (e.starts_at IS NULL OR e.starts_at <= now())
        AND (e.ends_at IS NULL OR e.ends_at > now())
        AND array_position(v_access_levels, e.access_level) >= v_min_level_idx
    ) INTO v_has_access;
    
    RETURN v_has_access;
END;
$$;

-- Get all active entitlements for a user
CREATE OR REPLACE FUNCTION public.get_user_entitlements(p_user_id UUID)
RETURNS TABLE (
    resource_type TEXT,
    resource_id TEXT,
    access_level TEXT,
    starts_at TIMESTAMPTZ,
    ends_at TIMESTAMPTZ,
    source TEXT
)
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
BEGIN
    RETURN QUERY
    SELECT 
        e.resource_type,
        e.resource_id,
        e.access_level,
        e.starts_at,
        e.ends_at,
        e.source
    FROM public.entitlements e
    WHERE e.user_id = p_user_id
    AND e.revoked_at IS NULL
    AND (e.starts_at IS NULL OR e.starts_at <= now())
    AND (e.ends_at IS NULL OR e.ends_at > now())
    ORDER BY e.created_at DESC;
END;
$$;

-- Get all users with entitlement to a resource (for rosters, room invites)
CREATE OR REPLACE FUNCTION public.get_resource_members(
    p_resource_type TEXT,
    p_resource_id TEXT
)
RETURNS TABLE (
    user_id UUID,
    email TEXT,
    access_level TEXT
)
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
BEGIN
    RETURN QUERY
    SELECT 
        e.user_id,
        e.email,
        e.access_level
    FROM public.entitlements e
    WHERE e.resource_type = p_resource_type
    AND e.resource_id = p_resource_id
    AND e.revoked_at IS NULL
    AND (e.starts_at IS NULL OR e.starts_at <= now())
    AND (e.ends_at IS NULL OR e.ends_at > now())
    ORDER BY e.access_level DESC, e.created_at ASC;
END;
$$;

-- Grant entitlement (used by webhook, admin UI)
CREATE OR REPLACE FUNCTION public.grant_entitlement(
    p_email TEXT,
    p_resource_type TEXT,
    p_resource_id TEXT,
    p_access_level TEXT DEFAULT 'participate',
    p_source TEXT DEFAULT 'admin_grant',
    p_source_id TEXT DEFAULT NULL,
    p_ends_at TIMESTAMPTZ DEFAULT NULL,
    p_stripe_checkout_session_id TEXT DEFAULT NULL,
    p_stripe_subscription_id TEXT DEFAULT NULL,
    p_stripe_customer_id TEXT DEFAULT NULL,
    p_metadata JSONB DEFAULT '{}'
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_user_id UUID;
    v_entitlement_id UUID;
BEGIN
    -- Try to find existing user by email
    SELECT id INTO v_user_id
    FROM auth.users
    WHERE email = lower(p_email);
    
    -- Upsert entitlement
    INSERT INTO public.entitlements (
        user_id,
        email,
        resource_type,
        resource_id,
        access_level,
        source,
        source_id,
        ends_at,
        stripe_checkout_session_id,
        stripe_subscription_id,
        stripe_customer_id,
        metadata
    ) VALUES (
        v_user_id,
        lower(p_email),
        p_resource_type,
        p_resource_id,
        p_access_level,
        p_source,
        p_source_id,
        p_ends_at,
        p_stripe_checkout_session_id,
        p_stripe_subscription_id,
        p_stripe_customer_id,
        p_metadata
    )
    ON CONFLICT (user_id, resource_type, resource_id) 
    DO UPDATE SET
        access_level = CASE 
            WHEN array_position(ARRAY['view', 'participate', 'host', 'admin'], EXCLUDED.access_level) >
                 array_position(ARRAY['view', 'participate', 'host', 'admin'], entitlements.access_level)
            THEN EXCLUDED.access_level
            ELSE entitlements.access_level
        END,
        ends_at = CASE 
            WHEN EXCLUDED.ends_at IS NULL THEN NULL  -- Perpetual wins
            WHEN entitlements.ends_at IS NULL THEN entitlements.ends_at  -- Keep perpetual
            WHEN EXCLUDED.ends_at > entitlements.ends_at THEN EXCLUDED.ends_at
            ELSE entitlements.ends_at
        END,
        updated_at = now(),
        metadata = entitlements.metadata || EXCLUDED.metadata
    RETURNING id INTO v_entitlement_id;
    
    RETURN v_entitlement_id;
END;
$$;

-- Revoke entitlement
CREATE OR REPLACE FUNCTION public.revoke_entitlement(
    p_user_id UUID,
    p_resource_type TEXT,
    p_resource_id TEXT,
    p_revoked_by TEXT,
    p_reason TEXT DEFAULT NULL
)
RETURNS BOOLEAN
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
BEGIN
    UPDATE public.entitlements
    SET 
        revoked_at = now(),
        revoked_by = p_revoked_by,
        revoked_reason = p_reason,
        updated_at = now()
    WHERE user_id = p_user_id
    AND resource_type = p_resource_type
    AND resource_id = p_resource_id
    AND revoked_at IS NULL;
    
    RETURN FOUND;
END;
$$;

-- ============================================================================
-- LINK ENTITLEMENTS TO USER ON SIGNUP
-- ============================================================================
-- When a user signs up, link any email-based entitlements to their user_id
CREATE OR REPLACE FUNCTION public.link_entitlements_to_user()
RETURNS TRIGGER
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
BEGIN
    -- Link any entitlements that were created for this email before the user existed
    UPDATE public.entitlements
    SET user_id = NEW.id
    WHERE email = lower(NEW.email)
    AND user_id IS NULL;
    
    RETURN NEW;
END;
$$;

-- Trigger to run on user creation
DROP TRIGGER IF EXISTS link_entitlements_on_user_signup ON auth.users;
CREATE TRIGGER link_entitlements_on_user_signup
    AFTER INSERT ON auth.users
    FOR EACH ROW
    EXECUTE FUNCTION public.link_entitlements_to_user();

-- ============================================================================
-- COMMENTS
-- ============================================================================
COMMENT ON TABLE public.entitlements IS 
'Central access control table. Payments create entitlements, content checks entitlements.';

COMMENT ON COLUMN public.entitlements.resource_type IS 
'Type of resource: arc, season, symposium, salon, archive, membership, faculty_club, matrix_room, lecture, credential';

COMMENT ON COLUMN public.entitlements.resource_id IS 
'Identifier for the resource (slug or ID). e.g., "more-human-than-human", "electric-sheep"';

COMMENT ON COLUMN public.entitlements.access_level IS 
'Level of access: view (read-only), participate (submit work), host (moderate), admin (full)';

COMMENT ON FUNCTION public.entitlement_is_active IS 
'Check if entitlement is active: within time bounds and not revoked';

COMMENT ON FUNCTION public.has_entitlement IS 
'Check if user has active entitlement for a resource. Used by access control everywhere.';

COMMENT ON FUNCTION public.grant_entitlement IS 
'Grant or upgrade an entitlement. Used by webhooks and admin UI.';
