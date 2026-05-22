-- Add MBTI field to faculty table for persona cognitive profiling
-- Per Inquiry Institute Persona Schema v1.1.0

-- Add mbti column (4-letter type code, e.g., "INTP", "ENFJ")
ALTER TABLE faculty 
ADD COLUMN IF NOT EXISTS mbti VARCHAR(4) 
CHECK (mbti IS NULL OR mbti ~ '^[EI][SN][TF][JP]$');

-- Add mbti_data column for full MBTI data (cognitive functions, evidence, etc.)
ALTER TABLE faculty 
ADD COLUMN IF NOT EXISTS mbti_data JSONB;

-- Add comment for documentation
COMMENT ON COLUMN faculty.mbti IS 'Myers-Briggs Type Indicator (4-letter code, e.g., INTP). Inferred from documented reasoning patterns per persona schema v1.1.0';
COMMENT ON COLUMN faculty.mbti_data IS 'Full MBTI data: confidence, dominant/auxiliary cognitive functions, evidence, behavioral markers';

-- Add index for potential filtering by type
CREATE INDEX IF NOT EXISTS idx_faculty_mbti ON faculty(mbti) WHERE mbti IS NOT NULL;

-- Example MBTI data structure (for reference):
-- {
--   "type": "INTP",
--   "confidence": "moderate",
--   "dominant_function": "Ti",
--   "auxiliary_function": "Ne",
--   "evidence": ["systematic logical analysis", "theoretical frameworks"],
--   "behavioral_markers": {
--     "energy_direction": "inward",
--     "information_gathering": "abstract",
--     "decision_making": "logical",
--     "lifestyle_orientation": "flexible"
--   }
-- }
