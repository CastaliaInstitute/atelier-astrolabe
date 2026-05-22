-- Matrix Bot Configuration and Conversation Storage
-- Supports serverless Matrix bots via Edge Functions (webhook and polling)

-- Matrix bot configurations
CREATE TABLE IF NOT EXISTS public.matrix_bots (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  
  -- Matrix credentials
  username text NOT NULL UNIQUE, -- e.g., "@a.plato:inquiry.institute"
  password text NOT NULL, -- Matrix password (encrypted/stored securely)
  
  -- Faculty association
  faculty_id text NOT NULL REFERENCES public.faculty(id),
  
  -- Room configuration
  room_ids text[], -- Optional: specific rooms to monitor (empty = all rooms)
  
  -- Sync state (for polling function)
  sync_token text, -- Matrix sync token for incremental sync
  last_sync timestamptz, -- Last successful sync timestamp
  
  -- Status
  active boolean DEFAULT true,
  
  -- Metadata
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Matrix conversation history storage
CREATE TABLE IF NOT EXISTS public.matrix_conversations (
  room_id text PRIMARY KEY,
  conversation_history jsonb DEFAULT '[]'::jsonb, -- Array of {role, content} messages
  last_message_at timestamptz DEFAULT now(),
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_matrix_bots_faculty_id 
  ON public.matrix_bots(faculty_id);

CREATE INDEX IF NOT EXISTS idx_matrix_bots_active 
  ON public.matrix_bots(active) WHERE active = true;

CREATE INDEX IF NOT EXISTS idx_matrix_bots_username 
  ON public.matrix_bots(username);

CREATE INDEX IF NOT EXISTS idx_matrix_conversations_last_message 
  ON public.matrix_conversations(last_message_at);

-- RLS policies
ALTER TABLE public.matrix_bots ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.matrix_conversations ENABLE ROW LEVEL SECURITY;

-- Allow public read (for debugging/monitoring)
CREATE POLICY "matrix_bots are publicly readable"
  ON public.matrix_bots
  FOR SELECT
  USING (true);

CREATE POLICY "matrix_conversations are publicly readable"
  ON public.matrix_conversations
  FOR SELECT
  USING (true);

-- Only service role can write (Edge Functions use service role)
-- This is handled by service role key, no policy needed for INSERT/UPDATE
