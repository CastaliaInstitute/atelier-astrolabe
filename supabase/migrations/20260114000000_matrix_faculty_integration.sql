-- Matrix Faculty Agent Integration Schema
-- Implements the design document for connecting llm-gateway to Matrix
-- Supports Mode A (Appservice) and Mode B (single bot) architectures

-- ============================================================================
-- FACULTY TABLE EXTENSIONS
-- ============================================================================

-- Add Matrix-specific fields to faculty table if they don't exist
DO $$ 
BEGIN
  -- Add matrix_user_localpart if not exists
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_name = 'faculty' AND column_name = 'matrix_user_localpart'
  ) THEN
    ALTER TABLE public.faculty 
    ADD COLUMN matrix_user_localpart text;
  END IF;

  -- Add default_model if not exists
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_name = 'faculty' AND column_name = 'default_model'
  ) THEN
    ALTER TABLE public.faculty 
    ADD COLUMN default_model text DEFAULT 'openai/gpt-oss-120b';
  END IF;

  -- Add system_prompt if not exists
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_name = 'faculty' AND column_name = 'system_prompt'
  ) THEN
    ALTER TABLE public.faculty 
    ADD COLUMN system_prompt text;
  END IF;

  -- Add safety_profile if not exists
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_name = 'faculty' AND column_name = 'safety_profile'
  ) THEN
    ALTER TABLE public.faculty 
    ADD COLUMN safety_profile jsonb DEFAULT '{}'::jsonb;
  END IF;

  -- Add enabled flag if not exists (rename from is_active if it exists)
  IF EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_name = 'faculty' AND column_name = 'is_active'
  ) THEN
    -- is_active exists, we'll use it as enabled
    NULL;
  ELSIF NOT EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_name = 'faculty' AND column_name = 'enabled'
  ) THEN
    ALTER TABLE public.faculty 
    ADD COLUMN enabled boolean DEFAULT true;
  END IF;
END $$;

-- Create index on matrix_user_localpart
CREATE INDEX IF NOT EXISTS idx_faculty_matrix_user_localpart 
  ON public.faculty(matrix_user_localpart) 
  WHERE matrix_user_localpart IS NOT NULL;

-- ============================================================================
-- MATRIX ROOMS TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.matrix_rooms (
  room_id text PRIMARY KEY, -- Matrix room ID (!abcdef:domain)
  name text NOT NULL,
  type text DEFAULT 'salon', -- salon, class, dm, etc.
  routing_mode text DEFAULT 'all', -- mention, all, command, moderated
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_matrix_rooms_routing_mode 
  ON public.matrix_rooms(routing_mode);

-- ============================================================================
-- ROOM FACULTY MEMBERSHIP
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.room_faculty_membership (
  room_id text NOT NULL REFERENCES public.matrix_rooms(room_id) ON DELETE CASCADE,
  faculty_id text NOT NULL REFERENCES public.faculty(id) ON DELETE CASCADE,
  role text DEFAULT 'speaker', -- speaker, moderator, listener
  priority int DEFAULT 0, -- arbitration order if multiple match
  created_at timestamptz DEFAULT now(),
  PRIMARY KEY (room_id, faculty_id)
);

CREATE INDEX IF NOT EXISTS idx_room_faculty_membership_room 
  ON public.room_faculty_membership(room_id);

CREATE INDEX IF NOT EXISTS idx_room_faculty_membership_faculty 
  ON public.room_faculty_membership(faculty_id);

CREATE INDEX IF NOT EXISTS idx_room_faculty_membership_priority 
  ON public.room_faculty_membership(room_id, priority DESC);

-- ============================================================================
-- CONVERSATIONS TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.conversations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  room_id text NOT NULL,
  thread_key text, -- Matrix thread ID ($thread_or_null)
  faculty_id text REFERENCES public.faculty(id) ON DELETE SET NULL,
  state jsonb DEFAULT '{}'::jsonb, -- memory, summary, last message ids
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  UNIQUE(room_id, thread_key, faculty_id)
);

CREATE INDEX IF NOT EXISTS idx_conversations_room_thread 
  ON public.conversations(room_id, thread_key);

CREATE INDEX IF NOT EXISTS idx_conversations_faculty 
  ON public.conversations(faculty_id);

-- ============================================================================
-- MESSAGE LOG (append-only audit)
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.message_log (
  event_id text PRIMARY KEY, -- Matrix event ID
  room_id text NOT NULL,
  sender text NOT NULL,
  ts bigint NOT NULL,
  body text NOT NULL,
  content_json jsonb DEFAULT '{}'::jsonb,
  ingested_at timestamptz DEFAULT now(),
  dedupe_hash text -- optional hash for additional deduplication
);

CREATE INDEX IF NOT EXISTS idx_message_log_room_ts 
  ON public.message_log(room_id, ts DESC);

CREATE INDEX IF NOT EXISTS idx_message_log_sender 
  ON public.message_log(sender);

CREATE INDEX IF NOT EXISTS idx_message_log_ingested 
  ON public.message_log(ingested_at DESC);

-- ============================================================================
-- RESPONSE LOG
-- ============================================================================

CREATE TABLE IF NOT EXISTS public.response_log (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  request_id text NOT NULL, -- from gateway
  room_id text NOT NULL,
  faculty_id text REFERENCES public.faculty(id) ON DELETE SET NULL,
  in_reply_to_event_id text, -- Matrix event being replied to
  matrix_sent_event_id text, -- Matrix event ID of sent response
  status text DEFAULT 'pending', -- pending|sent|failed
  error text,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_response_log_request_id 
  ON public.response_log(request_id);

CREATE INDEX IF NOT EXISTS idx_response_log_room_status 
  ON public.response_log(room_id, status);

CREATE INDEX IF NOT EXISTS idx_response_log_in_reply_to 
  ON public.response_log(in_reply_to_event_id);

CREATE INDEX IF NOT EXISTS idx_response_log_matrix_event 
  ON public.response_log(matrix_sent_event_id);

-- ============================================================================
-- UPDATE MATRIX_BOTS TABLE
-- ============================================================================

-- Add access_token and expiry to matrix_bots for persistence
DO $$ 
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_name = 'matrix_bots' AND column_name = 'access_token'
  ) THEN
    ALTER TABLE public.matrix_bots 
    ADD COLUMN access_token text,
    ADD COLUMN access_token_expiry timestamptz;
  END IF;
END $$;

CREATE INDEX IF NOT EXISTS idx_matrix_bots_access_token_expiry 
  ON public.matrix_bots(access_token_expiry) 
  WHERE access_token IS NOT NULL;

-- ============================================================================
-- ROW LEVEL SECURITY
-- ============================================================================

ALTER TABLE public.matrix_rooms ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.room_faculty_membership ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.conversations ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.message_log ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.response_log ENABLE ROW LEVEL SECURITY;

-- Public read for monitoring/debugging
DROP POLICY IF EXISTS "matrix_rooms are publicly readable" ON public.matrix_rooms;
CREATE POLICY "matrix_rooms are publicly readable"
  ON public.matrix_rooms FOR SELECT USING (true);

DROP POLICY IF EXISTS "room_faculty_membership is publicly readable" ON public.room_faculty_membership;
CREATE POLICY "room_faculty_membership is publicly readable"
  ON public.room_faculty_membership FOR SELECT USING (true);

DROP POLICY IF EXISTS "conversations are publicly readable" ON public.conversations;
CREATE POLICY "conversations are publicly readable"
  ON public.conversations FOR SELECT USING (true);

DROP POLICY IF EXISTS "message_log is publicly readable" ON public.message_log;
CREATE POLICY "message_log is publicly readable"
  ON public.message_log FOR SELECT USING (true);

DROP POLICY IF EXISTS "response_log is publicly readable" ON public.response_log;
CREATE POLICY "response_log is publicly readable"
  ON public.response_log FOR SELECT USING (true);

-- Service role can write (Edge Functions use service role key)
-- No explicit INSERT/UPDATE policies needed - service role bypasses RLS

-- Allow service role to insert/update (for testing, can be restricted later)
-- Note: Service role key bypasses RLS, but we add these for explicit clarity

-- ============================================================================
-- HELPER FUNCTIONS
-- ============================================================================

-- Function to get faculty agents for a room based on routing mode
CREATE OR REPLACE FUNCTION public.get_agents_for_room(
  p_room_id text,
  p_message_body text DEFAULT '',
  p_sender text DEFAULT ''
)
RETURNS TABLE (
  faculty_id text,
  slug text,
  display_name text,
  priority int,
  role text
) AS $$
BEGIN
  RETURN QUERY
  SELECT 
    f.id,
    f.id as slug, -- Use id as slug (faculty.id is like 'a.plato')
    f.name || COALESCE(' ' || f.surname, '') as display_name,
    rfm.priority,
    rfm.role
  FROM public.matrix_rooms mr
  JOIN public.room_faculty_membership rfm ON rfm.room_id = mr.room_id
  JOIN public.faculty f ON f.id = rfm.faculty_id
  WHERE 
    mr.room_id = p_room_id
    AND COALESCE(f.is_active, true) = true
    AND (
      -- Routing mode: all - everyone responds
      mr.routing_mode = 'all'
      OR
      -- Routing mode: mention - check for mentions
      (mr.routing_mode = 'mention' AND (
        p_message_body ILIKE '%@' || COALESCE(f.matrix_user_localpart, f.id) || '%'
        OR p_message_body ILIKE '%@' || f.id || '%'
        OR p_message_body ILIKE '%!' || f.id || '%'
        OR p_message_body ILIKE '%' || f.name || '%'
      ))
      OR
      -- Routing mode: command - check for !ask <id>
      (mr.routing_mode = 'command' AND p_message_body ~* ('!ask\s+' || f.id || '\b'))
    )
  ORDER BY 
    -- Explicit mentions first
    CASE WHEN p_message_body ILIKE '%@' || COALESCE(f.matrix_user_localpart, f.id) || '%' THEN 0 ELSE 1 END,
    rfm.priority DESC,
    f.id;
END;
$$ LANGUAGE plpgsql STABLE;

-- Function to get or create conversation
CREATE OR REPLACE FUNCTION public.get_or_create_conversation(
  p_room_id text,
  p_thread_key text DEFAULT NULL,
  p_faculty_id text DEFAULT NULL
)
RETURNS uuid AS $$
DECLARE
  v_conversation_id uuid;
BEGIN
  -- Try to find existing conversation
  SELECT id INTO v_conversation_id
  FROM public.conversations
  WHERE 
    room_id = p_room_id
    AND (thread_key = p_thread_key OR (thread_key IS NULL AND p_thread_key IS NULL))
    AND (faculty_id = p_faculty_id OR (faculty_id IS NULL AND p_faculty_id IS NULL))
  LIMIT 1;

  -- Create if not found
  IF v_conversation_id IS NULL THEN
    INSERT INTO public.conversations (room_id, thread_key, faculty_id)
    VALUES (p_room_id, p_thread_key, p_faculty_id)
    RETURNING id INTO v_conversation_id;
  END IF;

  RETURN v_conversation_id;
END;
$$ LANGUAGE plpgsql;
