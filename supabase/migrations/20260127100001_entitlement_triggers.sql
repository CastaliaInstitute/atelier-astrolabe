-- ============================================================================
-- ENTITLEMENT TRIGGERS
-- ============================================================================
-- Triggers that fire when entitlements are granted or revoked
-- to sync access to Matrix, WorkAdventure, etc.
-- ============================================================================

-- ============================================================================
-- HTTP EXTENSION (if not already enabled)
-- ============================================================================
CREATE EXTENSION IF NOT EXISTS http WITH SCHEMA extensions;

-- ============================================================================
-- FUNCTION: Notify on entitlement grant
-- ============================================================================
CREATE OR REPLACE FUNCTION public.notify_entitlement_grant()
RETURNS TRIGGER
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_response extensions.http_response;
    v_edge_function_url TEXT;
BEGIN
    -- Only fire for new grants (INSERT) or when entitlement becomes active
    IF TG_OP = 'INSERT' OR (TG_OP = 'UPDATE' AND NEW.is_active = true AND OLD.is_active = false) THEN
        -- Get edge function URL from secrets or use default
        v_edge_function_url := current_setting('app.edge_function_url', true);
        IF v_edge_function_url IS NULL THEN
            v_edge_function_url := 'https://your-project.supabase.co/functions/v1/grant-entitlement-access';
        END IF;
        
        -- Call edge function asynchronously (fire and forget for now)
        -- In production, use pg_net for true async or queue system
        BEGIN
            SELECT * INTO v_response FROM extensions.http_post(
                v_edge_function_url,
                jsonb_build_object(
                    'entitlement', jsonb_build_object(
                        'id', NEW.id,
                        'user_id', NEW.user_id,
                        'email', NEW.email,
                        'resource_type', NEW.resource_type,
                        'resource_id', NEW.resource_id,
                        'access_level', NEW.access_level,
                        'metadata', NEW.metadata
                    ),
                    'action', 'grant'
                )::text,
                'application/json'
            );
        EXCEPTION WHEN OTHERS THEN
            -- Log error but don't fail the transaction
            RAISE WARNING 'Failed to notify entitlement grant: %', SQLERRM;
        END;
    END IF;
    
    RETURN NEW;
END;
$$;

-- ============================================================================
-- FUNCTION: Notify on entitlement revoke
-- ============================================================================
CREATE OR REPLACE FUNCTION public.notify_entitlement_revoke()
RETURNS TRIGGER
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_response extensions.http_response;
    v_edge_function_url TEXT;
BEGIN
    -- Fire when entitlement is revoked
    IF TG_OP = 'UPDATE' AND NEW.revoked_at IS NOT NULL AND OLD.revoked_at IS NULL THEN
        v_edge_function_url := current_setting('app.edge_function_url', true);
        IF v_edge_function_url IS NULL THEN
            v_edge_function_url := 'https://your-project.supabase.co/functions/v1/grant-entitlement-access';
        END IF;
        
        BEGIN
            SELECT * INTO v_response FROM extensions.http_post(
                v_edge_function_url,
                jsonb_build_object(
                    'entitlement', jsonb_build_object(
                        'id', NEW.id,
                        'user_id', NEW.user_id,
                        'email', NEW.email,
                        'resource_type', NEW.resource_type,
                        'resource_id', NEW.resource_id,
                        'access_level', NEW.access_level,
                        'metadata', NEW.metadata
                    ),
                    'action', 'revoke'
                )::text,
                'application/json'
            );
        EXCEPTION WHEN OTHERS THEN
            RAISE WARNING 'Failed to notify entitlement revoke: %', SQLERRM;
        END;
    END IF;
    
    RETURN NEW;
END;
$$;

-- ============================================================================
-- TRIGGERS
-- ============================================================================

-- Note: These triggers are commented out by default because they require
-- the http extension and proper configuration of the edge function URL.
-- Uncomment after setting up the edge function.

-- CREATE TRIGGER trigger_entitlement_grant
--     AFTER INSERT OR UPDATE ON public.entitlements
--     FOR EACH ROW
--     EXECUTE FUNCTION public.notify_entitlement_grant();

-- CREATE TRIGGER trigger_entitlement_revoke  
--     AFTER UPDATE ON public.entitlements
--     FOR EACH ROW
--     EXECUTE FUNCTION public.notify_entitlement_revoke();

-- ============================================================================
-- ALTERNATIVE: Use pg_net for async HTTP calls (recommended for production)
-- ============================================================================
-- pg_net is Supabase's async HTTP extension that doesn't block transactions

-- CREATE OR REPLACE FUNCTION public.notify_entitlement_grant_async()
-- RETURNS TRIGGER
-- LANGUAGE plpgsql
-- SECURITY DEFINER
-- AS $$
-- BEGIN
--     IF TG_OP = 'INSERT' OR (TG_OP = 'UPDATE' AND NEW.is_active = true AND OLD.is_active = false) THEN
--         PERFORM net.http_post(
--             url := 'https://your-project.supabase.co/functions/v1/grant-entitlement-access',
--             body := jsonb_build_object(
--                 'entitlement', row_to_json(NEW),
--                 'action', 'grant'
--             ),
--             headers := '{"Content-Type": "application/json"}'::jsonb
--         );
--     END IF;
--     RETURN NEW;
-- END;
-- $$;

-- ============================================================================
-- ACTIVITY LOG TABLE (for tracking entitlement actions)
-- ============================================================================
CREATE TABLE IF NOT EXISTS public.activity_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    type TEXT NOT NULL,
    user_id UUID,
    email TEXT,
    resource_type TEXT,
    resource_id TEXT,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMPTZ DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_activity_log_type ON public.activity_log(type);
CREATE INDEX IF NOT EXISTS idx_activity_log_user_id ON public.activity_log(user_id);
CREATE INDEX IF NOT EXISTS idx_activity_log_created_at ON public.activity_log(created_at);

-- Enable RLS
ALTER TABLE public.activity_log ENABLE ROW LEVEL SECURITY;

-- Service role has full access
CREATE POLICY "Service role full access to activity_log" ON public.activity_log
    FOR ALL USING (auth.role() = 'service_role');

COMMENT ON TABLE public.activity_log IS 'Audit log for entitlement grants, revokes, and other significant actions';
