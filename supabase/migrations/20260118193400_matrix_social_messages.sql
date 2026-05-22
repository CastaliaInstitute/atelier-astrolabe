-- Matrix Social Messages - DCIO Review Queue
-- Messages from Social Matrix room that need DCIO approval before posting

CREATE TABLE IF NOT EXISTS public.matrix_social_messages (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  
  -- Matrix event info
  matrix_event_id text NOT NULL,
  matrix_room_id text NOT NULL,
  matrix_sender text NOT NULL,
  
  -- Message content
  message_body text NOT NULL,
  message_type text NOT NULL DEFAULT 'text', -- text, image, etc.
  
  -- Review status
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'approved', 'rejected', 'posted', 'failed')),
  
  -- Review metadata
  reviewed_by text, -- DCIO user/faculty ID (e.g., 'a.woolf')
  reviewed_at timestamptz,
  review_notes text, -- Notes from DCIO
  
  -- Posting metadata (after approval)
  posted_to_platform text DEFAULT 'gotosocial', -- gotosocial, linkedin, etc.
  post_url text, -- URL of posted status
  post_id text, -- Platform post ID
  posted_at timestamptz,
  
  -- Error tracking
  error_message text,
  
  -- Timestamps
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_matrix_social_messages_status ON public.matrix_social_messages(status);
CREATE INDEX IF NOT EXISTS idx_matrix_social_messages_room ON public.matrix_social_messages(matrix_room_id);
CREATE INDEX IF NOT EXISTS idx_matrix_social_messages_pending ON public.matrix_social_messages(status) WHERE status = 'pending';
CREATE INDEX IF NOT EXISTS idx_matrix_social_messages_event ON public.matrix_social_messages(matrix_event_id);

-- Enable RLS
ALTER TABLE public.matrix_social_messages ENABLE ROW LEVEL SECURITY;

-- Policy: Anyone can read (for transparency)
CREATE POLICY "Anyone can read social messages"
  ON public.matrix_social_messages
  FOR SELECT
  USING (true);

-- Policy: Authenticated users can insert (for matrix-bridge)
CREATE POLICY "Authenticated users can insert social messages"
  ON public.matrix_social_messages
  FOR INSERT
  WITH CHECK (auth.uid() IS NOT NULL);

-- Policy: DCIO can update (approve/reject/post)
-- TODO: Tighten to require DCIO role once roles table exists
CREATE POLICY "Authenticated users can update social messages"
  ON public.matrix_social_messages
  FOR UPDATE
  USING (auth.uid() IS NOT NULL)
  WITH CHECK (auth.uid() IS NOT NULL);

-- Function to update updated_at timestamp
CREATE OR REPLACE FUNCTION update_matrix_social_messages_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Trigger to auto-update updated_at
CREATE TRIGGER update_matrix_social_messages_updated_at
  BEFORE UPDATE ON public.matrix_social_messages
  FOR EACH ROW
  EXECUTE FUNCTION update_matrix_social_messages_updated_at();
