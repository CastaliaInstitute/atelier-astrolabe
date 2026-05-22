-- Daily message limits for authenticated chat (faculty_chat, ask_book).
-- Anonymous caps use public_edge_chat_usage + consume_public_edge_chat (separate migration).
-- Tiers: registered (signed-in, no membership) vs member (active membership entitlement).

CREATE TABLE IF NOT EXISTS public.feature_daily_usage (
  user_id uuid NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
  feature text NOT NULL,
  usage_date date NOT NULL,
  message_count int NOT NULL DEFAULT 0,
  updated_at timestamptz DEFAULT now(),
  PRIMARY KEY (user_id, feature, usage_date)
);

ALTER TABLE public.feature_daily_usage ENABLE ROW LEVEL SECURITY;

CREATE INDEX IF NOT EXISTS idx_feature_daily_usage_date
  ON public.feature_daily_usage (usage_date);

CREATE OR REPLACE FUNCTION public.resolve_chat_tier(p_user_id uuid)
RETURNS text
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
BEGIN
  IF EXISTS (
    SELECT 1
    FROM public.entitlements e
    WHERE e.user_id = p_user_id
      AND e.resource_type = 'membership'
      AND e.revoked_at IS NULL
      AND (e.starts_at IS NULL OR e.starts_at <= now())
      AND (e.ends_at IS NULL OR e.ends_at > now())
  ) THEN
    RETURN 'member';
  END IF;
  RETURN 'registered';
END;
$$;

CREATE OR REPLACE FUNCTION public.chat_feature_daily_limit(p_tier text, p_feature text)
RETURNS int
LANGUAGE sql
IMMUTABLE
AS $$
  SELECT CASE
    WHEN p_tier = 'member' THEN NULL::int
    WHEN p_tier = 'registered' AND p_feature IN ('faculty_chat', 'ask_book') THEN 20
    ELSE 0
  END;
$$;

CREATE OR REPLACE FUNCTION public._consume_feature_daily(
  p_user_id uuid,
  p_feature text,
  p_daily_max int,
  p_increment boolean
) RETURNS jsonb
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
  v_today date := (timezone('utc', now()))::date;
  v_tier text;
  v_limit int;
  v_new_count int;
  v_used int;
BEGIN
  v_tier := public.resolve_chat_tier(p_user_id);
  v_limit := COALESCE(p_daily_max, public.chat_feature_daily_limit(v_tier, p_feature));

  IF v_limit IS NULL THEN
    RETURN jsonb_build_object(
      'allowed', true,
      'tier', v_tier,
      'daily_limit', NULL,
      'used', NULL,
      'remaining', NULL,
      'upgrade_required', false
    );
  END IF;

  IF NOT p_increment THEN
    SELECT message_count INTO v_used
    FROM public.feature_daily_usage
    WHERE user_id = p_user_id
      AND feature = p_feature
      AND usage_date = v_today;

    v_used := COALESCE(v_used, 0);
    RETURN jsonb_build_object(
      'allowed', v_used < v_limit,
      'tier', v_tier,
      'daily_limit', v_limit,
      'used', v_used,
      'remaining', GREATEST(v_limit - v_used, 0),
      'upgrade_required', v_tier = 'registered',
      'reason', CASE WHEN v_used >= v_limit THEN 'daily_limit_reached' ELSE NULL END
    );
  END IF;

  INSERT INTO public.feature_daily_usage AS u (user_id, feature, usage_date, message_count)
  VALUES (p_user_id, p_feature, v_today, 1)
  ON CONFLICT (user_id, feature, usage_date)
  DO UPDATE SET
    message_count = u.message_count + 1,
    updated_at = now()
  RETURNING message_count INTO v_new_count;

  IF v_new_count > v_limit THEN
    RETURN jsonb_build_object(
      'allowed', false,
      'tier', v_tier,
      'daily_limit', v_limit,
      'used', v_new_count,
      'remaining', 0,
      'upgrade_required', v_tier = 'registered',
      'reason', 'daily_limit_reached'
    );
  END IF;

  RETURN jsonb_build_object(
    'allowed', true,
    'tier', v_tier,
    'daily_limit', v_limit,
    'used', v_new_count,
    'remaining', v_limit - v_new_count,
    'upgrade_required', false
  );
END;
$$;

CREATE OR REPLACE FUNCTION public.use_feature(p_user_id uuid, p_feature text)
RETURNS jsonb
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
BEGIN
  IF p_feature NOT IN ('faculty_chat', 'ask_book') THEN
    RETURN jsonb_build_object(
      'allowed', false,
      'reason', 'unknown_feature',
      'tier', public.resolve_chat_tier(p_user_id)
    );
  END IF;
  RETURN public._consume_feature_daily(p_user_id, p_feature, NULL, true);
END;
$$;

CREATE OR REPLACE FUNCTION public.check_feature(p_user_id uuid, p_feature text)
RETURNS jsonb
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
BEGIN
  IF p_feature NOT IN ('faculty_chat', 'ask_book') THEN
    RETURN jsonb_build_object(
      'allowed', false,
      'reason', 'unknown_feature',
      'tier', public.resolve_chat_tier(p_user_id)
    );
  END IF;
  RETURN public._consume_feature_daily(p_user_id, p_feature, NULL, false);
END;
$$;

GRANT EXECUTE ON FUNCTION public.resolve_chat_tier(uuid) TO service_role;
GRANT EXECUTE ON FUNCTION public.chat_feature_daily_limit(text, text) TO service_role;
GRANT EXECUTE ON FUNCTION public.use_feature(uuid, text) TO service_role;
GRANT EXECUTE ON FUNCTION public.check_feature(uuid, text) TO service_role;
