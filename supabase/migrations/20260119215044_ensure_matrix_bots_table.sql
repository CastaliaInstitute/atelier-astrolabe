-- Ensure matrix_bots table exists with all required columns
CREATE TABLE IF NOT EXISTS public.matrix_bots (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  
  -- Matrix credentials
  username text NOT NULL UNIQUE, -- e.g., "@a.plato:matrix.inquiry.institute"
  password text NOT NULL, -- Matrix password (encrypted/stored securely)
  
  -- Faculty association
  faculty_id text NOT NULL REFERENCES public.faculty(id),
  
  -- Room configuration
  room_ids text[], -- Optional: specific rooms to monitor (empty = all rooms)
  
  -- Sync state (for polling function)
  sync_token text, -- Matrix sync token for incremental sync
  last_sync timestamptz, -- Last successful sync timestamp
  
  -- Access token for Matrix API
  access_token text,
  access_token_expiry timestamptz,
  
  -- Status
  active boolean DEFAULT true,
  
  -- Metadata
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Add access_token columns if they don't exist
DO $$ 
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns 
    WHERE table_schema = 'public' 
    AND table_name = 'matrix_bots' 
    AND column_name = 'access_token'
  ) THEN
    ALTER TABLE public.matrix_bots 
    ADD COLUMN access_token text,
    ADD COLUMN access_token_expiry timestamptz;
  END IF;
END $$;

-- Indexes
CREATE INDEX IF NOT EXISTS idx_matrix_bots_faculty_id 
  ON public.matrix_bots(faculty_id);

CREATE INDEX IF NOT EXISTS idx_matrix_bots_active 
  ON public.matrix_bots(active) WHERE active = true;

CREATE INDEX IF NOT EXISTS idx_matrix_bots_username 
  ON public.matrix_bots(username);

CREATE INDEX IF NOT EXISTS idx_matrix_bots_access_token_expiry 
  ON public.matrix_bots(access_token_expiry) 
  WHERE access_token IS NOT NULL;

-- RLS policies
ALTER TABLE public.matrix_bots ENABLE ROW LEVEL SECURITY;

-- Allow public read (for debugging/monitoring)
DROP POLICY IF EXISTS "matrix_bots are publicly readable" ON public.matrix_bots;
CREATE POLICY "matrix_bots are publicly readable"
  ON public.matrix_bots
  FOR SELECT
  USING (true);
