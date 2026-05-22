-- Faculty Club Matrix Rooms
-- Stores Matrix room IDs for faculty.club tables (both static and dynamic)

CREATE TABLE IF NOT EXISTS public.faculty_club_matrix_rooms (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  
  -- Table identification
  table_id text UNIQUE NOT NULL, -- Static table ID (e.g., 'lsd-discussion') or dynamic ID (UUID)
  table_name text NOT NULL,
  table_type text NOT NULL DEFAULT 'static', -- 'static' or 'dynamic'
  
  -- Matrix room information
  room_id text UNIQUE NOT NULL, -- Matrix room ID (e.g., '!abc123:inquiry.institute')
  room_name text NOT NULL,
  room_alias text, -- Optional room alias
  
  -- Table metadata
  topic text,
  description text,
  question text, -- For dynamic tables created from inquiry form
  
  -- Participants
  participants text[] NOT NULL DEFAULT '{}', -- Array of faculty slugs (e.g., ['a.leary', 'a.huxley'])
  
  -- Status
  active boolean DEFAULT true,
  
  -- Metadata
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  created_by uuid REFERENCES auth.users(id), -- User who created the table (for dynamic tables)
  
  -- Constraints
  CONSTRAINT valid_table_type CHECK (table_type IN ('static', 'dynamic'))
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_faculty_club_matrix_rooms_table_id 
  ON public.faculty_club_matrix_rooms(table_id);

CREATE INDEX IF NOT EXISTS idx_faculty_club_matrix_rooms_room_id 
  ON public.faculty_club_matrix_rooms(room_id);

CREATE INDEX IF NOT EXISTS idx_faculty_club_matrix_rooms_table_type 
  ON public.faculty_club_matrix_rooms(table_type);

CREATE INDEX IF NOT EXISTS idx_faculty_club_matrix_rooms_active 
  ON public.faculty_club_matrix_rooms(active) WHERE active = true;

CREATE INDEX IF NOT EXISTS idx_faculty_club_matrix_rooms_created_by 
  ON public.faculty_club_matrix_rooms(created_by);

-- RLS policies
ALTER TABLE public.faculty_club_matrix_rooms ENABLE ROW LEVEL SECURITY;

-- Allow public read (for room lookups)
CREATE POLICY "faculty_club_matrix_rooms are publicly readable"
  ON public.faculty_club_matrix_rooms
  FOR SELECT
  USING (true);

-- Allow authenticated users to create dynamic rooms
CREATE POLICY "authenticated users can create dynamic rooms"
  ON public.faculty_club_matrix_rooms
  FOR INSERT
  WITH CHECK (
    auth.role() = 'authenticated' AND
    (table_type = 'dynamic' OR table_type = 'static')
  );

-- Allow users to update their own dynamic rooms
CREATE POLICY "users can update their own dynamic rooms"
  ON public.faculty_club_matrix_rooms
  FOR UPDATE
  USING (
    auth.role() = 'authenticated' AND
    (created_by = auth.uid() OR table_type = 'static')
  );

-- Add comment
COMMENT ON TABLE public.faculty_club_matrix_rooms IS 'Matrix room mappings for faculty.club tables. Supports both static tables (from faculty-club-tables.ts) and dynamic tables (created from inquiry search form).';
