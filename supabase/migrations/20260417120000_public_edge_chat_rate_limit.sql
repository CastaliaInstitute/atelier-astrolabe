-- Daily usage caps for anonymous (anon-key) chat traffic, keyed by hashed bucket (IP + feature).
-- Authenticated members continue to use use_feature() / faculty_chat + ask_book tiers.

CREATE TABLE IF NOT EXISTS public.public_edge_chat_usage (
  bucket_key text NOT NULL,
  usage_date date NOT NULL,
  message_count int NOT NULL DEFAULT 0,
  updated_at timestamptz DEFAULT now(),
  PRIMARY KEY (bucket_key, usage_date)
);

ALTER TABLE public.public_edge_chat_usage ENABLE ROW LEVEL SECURITY;

CREATE INDEX IF NOT EXISTS idx_public_edge_chat_usage_date
  ON public.public_edge_chat_usage (usage_date);

CREATE OR REPLACE FUNCTION public.consume_public_edge_chat(
  p_bucket_key text,
  p_daily_max int
) RETURNS jsonb
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
  v_today date := (timezone('utc', now()))::date;
  v_new_count int;
BEGIN
  INSERT INTO public.public_edge_chat_usage AS u (bucket_key, usage_date, message_count)
  VALUES (p_bucket_key, v_today, 1)
  ON CONFLICT (bucket_key, usage_date)
  DO UPDATE SET
    message_count = u.message_count + 1,
    updated_at = now()
  RETURNING message_count INTO v_new_count;

  IF v_new_count > p_daily_max THEN
    RETURN jsonb_build_object(
      'allowed', false,
      'used', v_new_count,
      'limit', p_daily_max,
      'remaining', 0
    );
  END IF;

  RETURN jsonb_build_object(
    'allowed', true,
    'used', v_new_count,
    'limit', p_daily_max,
    'remaining', p_daily_max - v_new_count
  );
END;
$$;

GRANT EXECUTE ON FUNCTION public.consume_public_edge_chat(text, int) TO service_role;
