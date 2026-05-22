-- Debug log table for matrix-bridge execution flow
-- Allows us to see what's happening without dashboard access

CREATE TABLE IF NOT EXISTS public.matrix_bridge_debug_log (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  event_id text,
  room_id text,
  faculty_id text,
  step text NOT NULL, -- 'start', 'routing', 'llm_call', 'token_get', 'matrix_send', 'error'
  message text,
  data jsonb,
  created_at timestamptz DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_matrix_bridge_debug_log_event_id 
  ON public.matrix_bridge_debug_log(event_id);

CREATE INDEX IF NOT EXISTS idx_matrix_bridge_debug_log_created_at 
  ON public.matrix_bridge_debug_log(created_at DESC);

-- RLS
ALTER TABLE public.matrix_bridge_debug_log ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "matrix_bridge_debug_log is publicly readable" ON public.matrix_bridge_debug_log;
CREATE POLICY "matrix_bridge_debug_log is publicly readable"
  ON public.matrix_bridge_debug_log FOR SELECT USING (true);
