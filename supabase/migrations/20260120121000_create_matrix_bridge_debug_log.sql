-- Create debug log table for matrix-bridge function
-- This helps track processing steps and identify failures

CREATE TABLE IF NOT EXISTS public.matrix_bridge_debug_log (
  id BIGSERIAL PRIMARY KEY,
  event_id TEXT,
  room_id TEXT,
  faculty_id TEXT,
  step TEXT NOT NULL,
  message TEXT,
  data JSONB,
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Add index for querying by room and event
CREATE INDEX IF NOT EXISTS idx_matrix_bridge_debug_log_room_event 
  ON public.matrix_bridge_debug_log(room_id, event_id);

CREATE INDEX IF NOT EXISTS idx_matrix_bridge_debug_log_created 
  ON public.matrix_bridge_debug_log(created_at DESC);

-- Enable RLS
ALTER TABLE public.matrix_bridge_debug_log ENABLE ROW LEVEL SECURITY;

-- Allow service role to insert (for Edge Functions)
CREATE POLICY "Service role can insert debug logs"
  ON public.matrix_bridge_debug_log
  FOR INSERT
  TO service_role
  WITH CHECK (true);

-- Allow service role to read debug logs
CREATE POLICY "Service role can read debug logs"
  ON public.matrix_bridge_debug_log
  FOR SELECT
  TO service_role
  USING (true);
