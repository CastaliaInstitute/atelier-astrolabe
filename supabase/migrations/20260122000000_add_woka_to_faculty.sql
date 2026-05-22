-- Add Woka (WorkAdventure avatar) customization field to faculty table
-- Stores WorkAdventure avatar configuration as JSONB

ALTER TABLE public.faculty
ADD COLUMN IF NOT EXISTS woka jsonb DEFAULT NULL;

-- Add comment explaining the structure
COMMENT ON COLUMN public.faculty.woka IS 'WorkAdventure avatar (Woka) customization data. JSONB structure:
{
  "texture_id": "string",           // Main texture collection ID
  "body": "string",                 // Body texture ID
  "hair": "string",                 // Hair texture ID
  "eyes": "string",                 // Eyes texture ID
  "accessories": ["string"],        // Array of accessory texture IDs
  "colors": {                       // Color customization
    "skin": "string",
    "hair": "string",
    "eyes": "string"
  },
  "metadata": {                     // Additional metadata
    "created_at": "timestamp",
    "updated_at": "timestamp",
    "source": "manual|generated|imported"
  }
}';

-- Create GIN index for efficient JSONB queries
CREATE INDEX IF NOT EXISTS idx_faculty_woka 
ON public.faculty USING GIN(woka) 
WHERE woka IS NOT NULL;

-- Add helpful comment
COMMENT ON INDEX idx_faculty_woka IS 'GIN index for efficient querying of Woka customization data';
