-- Matrix Bot Room Assignments and Locations
-- Extends matrix_bots to support room assignments and positions

-- Matrix bot room assignments
CREATE TABLE IF NOT EXISTS public.matrix_bot_rooms (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  
  -- Bot reference
  bot_id uuid NOT NULL REFERENCES public.matrix_bots(id) ON DELETE CASCADE,
  
  -- Room configuration
  room_alias text, -- e.g., "#workadventure:inquiry.institute"
  room_id text, -- Matrix room ID (e.g., "!abc123:inquiry.institute")
  
  -- Location/Position (for WorkAdventure or other spatial interfaces)
  -- Stored as JSONB to be flexible for different room types
  -- Example formats:
  --   WorkAdventure: {"x": 100, "y": 200, "direction": "down", "seat_position": "left"}
  --   Table seating: {"x": 100, "y": 200, "seat_position": "left", "rotation": 180}
  --   Desk: {"x": 150, "y": 250, "direction": "right", "seat_position": "center"}
  -- Fields:
  --   x, y: coordinates (required)
  --   direction: "up", "down", "left", "right" (facing direction in WorkAdventure)
  --   seat_position: "left", "right", "center" (position at table/desk)
  --   rotation: degrees 0-360 (precise rotation)
  --   layer: "ground", "floor1", etc. (map layer)
  position jsonb,
  
  -- Room metadata
  room_name text, -- Human-readable room name (e.g., "WorkAdventure University")
  room_type text, -- e.g., "workadventure", "element", "general"
  
  -- Status
  active boolean DEFAULT true,
  auto_join boolean DEFAULT true, -- Automatically join room
  
  -- Metadata
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  
  -- Constraints
  CONSTRAINT room_alias_or_id CHECK (
    (room_alias IS NOT NULL AND room_alias != '') OR 
    (room_id IS NOT NULL AND room_id != '')
  )
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_matrix_bot_rooms_bot_id 
  ON public.matrix_bot_rooms(bot_id);

CREATE INDEX IF NOT EXISTS idx_matrix_bot_rooms_room_id 
  ON public.matrix_bot_rooms(room_id);

CREATE INDEX IF NOT EXISTS idx_matrix_bot_rooms_room_alias 
  ON public.matrix_bot_rooms(room_alias);

CREATE INDEX IF NOT EXISTS idx_matrix_bot_rooms_active 
  ON public.matrix_bot_rooms(active) WHERE active = true;

-- RLS policies
ALTER TABLE public.matrix_bot_rooms ENABLE ROW LEVEL SECURITY;

-- Allow public read (for debugging/monitoring)
CREATE POLICY "matrix_bot_rooms are publicly readable"
  ON public.matrix_bot_rooms
  FOR SELECT
  USING (true);

-- Add comment
COMMENT ON TABLE public.matrix_bot_rooms IS 'Room assignments and locations for Matrix bots. Supports multiple rooms per bot with positions/locations.';
