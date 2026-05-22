-- Add per-course entitlements (GitHub Pages hostnames like ain6001.courses.castalia.institute)
-- and Custom Access Token Hook for Cloudflare Access OIDC claim checks.

-- ============================================================================
-- 1. Extend resource_type to include `course`
-- ============================================================================
ALTER TABLE public.entitlements DROP CONSTRAINT IF EXISTS entitlements_resource_type_check;

ALTER TABLE public.entitlements ADD CONSTRAINT entitlements_resource_type_check CHECK (
  resource_type IN (
    'arc',
    'season',
    'symposium',
    'salon',
    'archive',
    'membership',
    'faculty_club',
    'matrix_room',
    'lecture',
    'credential',
    'course'
  )
);

COMMENT ON COLUMN public.entitlements.resource_type IS
  'Resource category. Use course + resource_id (e.g. ain6001) for per-course GitHub Pages sites; JWT hook emits app_metadata claims like course_ain6001.';

-- ============================================================================
-- 2. Custom Access Token Hook — inject entitlement booleans into JWT app_metadata
-- ============================================================================
-- Claim keys: {resource_type}_{sanitized_resource_id}, e.g. course_ain6001, salon_villadiodati
-- Enable in Dashboard: Authentication → Hooks → Custom access token → Postgres → public.custom_access_token_hook

CREATE OR REPLACE FUNCTION public.custom_access_token_hook(event jsonb)
RETURNS jsonb
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
  claims jsonb;
  uid uuid;
  ent RECORD;
  meta jsonb;
  claim_key text;
BEGIN
  claims := event->'claims';
  IF claims IS NULL THEN
    RETURN event;
  END IF;

  BEGIN
    uid := (event->>'user_id')::uuid;
  EXCEPTION WHEN OTHERS THEN
    RETURN event;
  END;

  IF uid IS NULL THEN
    RETURN event;
  END IF;

  meta := COALESCE(claims->'app_metadata', '{}'::jsonb);

  FOR ent IN
    SELECT e.resource_type, e.resource_id
    FROM public.get_user_entitlements(uid) AS e
  LOOP
    claim_key := ent.resource_type || '_' || regexp_replace(
      ent.resource_id,
      '[^a-zA-Z0-9_-]',
      '_',
      'g'
    );
    meta := meta || jsonb_build_object(claim_key, true);
  END LOOP;

  claims := jsonb_set(claims, '{app_metadata}', meta, true);

  RETURN jsonb_build_object('claims', claims);
END;
$$;

COMMENT ON FUNCTION public.custom_access_token_hook(jsonb) IS
  'Auth hook: merges active entitlements into JWT app_metadata as boolean flags (e.g. course_ain6001). Grant execute only to supabase_auth_admin; enable in Auth → Hooks.';

REVOKE ALL ON FUNCTION public.custom_access_token_hook(jsonb) FROM PUBLIC;
GRANT EXECUTE ON FUNCTION public.custom_access_token_hook(jsonb) TO supabase_auth_admin;
