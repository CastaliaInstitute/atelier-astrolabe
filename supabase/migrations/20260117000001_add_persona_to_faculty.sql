-- Add persona JSONB field to faculty table
-- Stores the full persona schema per Design Document v1.1

ALTER TABLE faculty 
ADD COLUMN IF NOT EXISTS persona JSONB;

-- Add comment for documentation
COMMENT ON COLUMN faculty.persona IS 'Full persona schema (conversational_posture, epistemic_stance, argumentative_mechanics, etc.) per Design Document v1.1';

-- Add GIN index for JSONB queries
CREATE INDEX IF NOT EXISTS idx_faculty_persona ON faculty USING GIN (persona) WHERE persona IS NOT NULL;

-- Example persona structure (for reference):
-- {
--   "conversational_posture": {
--     "default_stance": "socratic",
--     "turn_taking": { "initiative": 0.7, "question_frequency": 0.8, "elaboration_tendency": 0.6 },
--     "register": { "formality": 0.7, "technical_density": 0.8, "metaphor_use": "abundant" },
--     "characteristic_moves": ["elenchus", "aporia", "maieutic questioning"]
--   },
--   "epistemic_stance": {
--     "certainty_orientation": "provisional",
--     "evidence_hierarchy": ["reason", "dialogue", "experience"],
--     "revision_openness": 0.8,
--     "truth_conception": "correspondence",
--     "acknowledged_limits": ["cannot know the Forms directly", "human wisdom is limited"]
--   },
--   "argumentative_mechanics": { ... },
--   "ethical_orientation": { ... },
--   "affective_envelope": { ... },
--   "stress_response": { ... }
-- }
