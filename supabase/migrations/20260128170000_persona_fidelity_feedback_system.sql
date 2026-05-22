-- ============================================================================
-- PERSONA FIDELITY FEEDBACK SYSTEM
-- Enables ask-faculty to self-reflect on responses and update context prompts
-- ============================================================================

-- ============================================================================
-- 1. FACULTY CONTEXT PROMPTS TABLE
-- Dynamic, updateable context prompts per faculty (can be refined over time)
-- ============================================================================
CREATE TABLE IF NOT EXISTS public.faculty_context_prompts (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  faculty_id TEXT NOT NULL REFERENCES public.faculty(id) ON DELETE CASCADE,
  context_type TEXT NOT NULL DEFAULT 'general', -- 'general', 'symposium', 'lecture', 'dialogue', etc.
  context_key TEXT, -- e.g., 'ayandeh-ye-iran' for symposium-specific
  
  -- The prompt content
  base_prompt TEXT NOT NULL,
  fidelity_instructions TEXT, -- Specific instructions for maintaining voice
  anti_patterns TEXT[], -- Patterns to avoid (learned from failures)
  reinforcement_patterns TEXT[], -- Patterns that work well (learned from successes)
  
  -- Versioning
  version INTEGER NOT NULL DEFAULT 1,
  is_active BOOLEAN NOT NULL DEFAULT true,
  
  -- Metadata
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  created_by TEXT, -- 'system', 'human_review', 'auto_refinement'
  
  UNIQUE(faculty_id, context_type, context_key, version)
);

-- Index for fast lookups
CREATE INDEX IF NOT EXISTS idx_faculty_context_prompts_lookup 
  ON public.faculty_context_prompts(faculty_id, context_type, context_key, is_active);

-- ============================================================================
-- 2. PERSONA FIDELITY EVALUATIONS TABLE
-- Logs self-reflection evaluations for analysis and learning
-- ============================================================================
CREATE TABLE IF NOT EXISTS public.persona_fidelity_evaluations (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  faculty_id TEXT NOT NULL REFERENCES public.faculty(id) ON DELETE CASCADE,
  
  -- The generation context
  user_message TEXT NOT NULL,
  generated_response TEXT NOT NULL,
  context_type TEXT,
  context_key TEXT,
  model_used TEXT,
  
  -- Evaluation results
  fidelity_score DECIMAL(3,2), -- 0.00 to 1.00
  passed BOOLEAN NOT NULL,
  
  -- Detailed evaluation
  evaluation_breakdown JSONB, -- { "voice_consistency": 0.8, "position_locks": 1.0, ... }
  violations_detected TEXT[], -- List of specific violations
  strengths_detected TEXT[], -- What worked well
  
  -- Evaluator metadata
  evaluator_model TEXT,
  evaluation_prompt_version TEXT,
  
  -- If regeneration occurred
  was_regenerated BOOLEAN DEFAULT false,
  regeneration_attempt INTEGER DEFAULT 0,
  final_response TEXT, -- If different from generated_response after regeneration
  
  -- Timestamps
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Index for analysis queries
CREATE INDEX IF NOT EXISTS idx_fidelity_evaluations_faculty 
  ON public.persona_fidelity_evaluations(faculty_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_fidelity_evaluations_failures 
  ON public.persona_fidelity_evaluations(faculty_id, passed) WHERE NOT passed;

-- ============================================================================
-- 3. PROMPT REFINEMENT SUGGESTIONS TABLE
-- Captures suggestions for improving prompts based on evaluation patterns
-- ============================================================================
CREATE TABLE IF NOT EXISTS public.prompt_refinement_suggestions (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  faculty_id TEXT NOT NULL REFERENCES public.faculty(id) ON DELETE CASCADE,
  context_prompt_id UUID REFERENCES public.faculty_context_prompts(id),
  
  -- The suggestion
  suggestion_type TEXT NOT NULL, -- 'add_anti_pattern', 'add_reinforcement', 'modify_instruction', 'position_lock_violation'
  suggestion_content TEXT NOT NULL,
  evidence_evaluation_ids UUID[], -- Links to evaluations that led to this suggestion
  
  -- Status
  status TEXT NOT NULL DEFAULT 'pending', -- 'pending', 'applied', 'rejected', 'deferred'
  applied_at TIMESTAMPTZ,
  applied_by TEXT,
  rejection_reason TEXT,
  
  -- Metadata
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  confidence DECIMAL(3,2) -- How confident we are this will help
);

-- ============================================================================
-- 4. HELPER FUNCTION: Get active context prompt for faculty
-- ============================================================================
CREATE OR REPLACE FUNCTION get_faculty_context_prompt(
  p_faculty_id TEXT,
  p_context_type TEXT DEFAULT 'general',
  p_context_key TEXT DEFAULT NULL
)
RETURNS TABLE (
  prompt_id UUID,
  base_prompt TEXT,
  fidelity_instructions TEXT,
  anti_patterns TEXT[],
  reinforcement_patterns TEXT[],
  version INTEGER
)
LANGUAGE plpgsql
AS $$
BEGIN
  RETURN QUERY
  SELECT 
    fcp.id,
    fcp.base_prompt,
    fcp.fidelity_instructions,
    fcp.anti_patterns,
    fcp.reinforcement_patterns,
    fcp.version
  FROM public.faculty_context_prompts fcp
  WHERE fcp.faculty_id = p_faculty_id
    AND fcp.context_type = p_context_type
    AND (fcp.context_key = p_context_key OR (fcp.context_key IS NULL AND p_context_key IS NULL))
    AND fcp.is_active = true
  ORDER BY fcp.version DESC
  LIMIT 1;
END;
$$;

-- ============================================================================
-- 5. HELPER FUNCTION: Log fidelity evaluation
-- ============================================================================
CREATE OR REPLACE FUNCTION log_fidelity_evaluation(
  p_faculty_id TEXT,
  p_user_message TEXT,
  p_generated_response TEXT,
  p_fidelity_score DECIMAL(3,2),
  p_passed BOOLEAN,
  p_evaluation_breakdown JSONB DEFAULT NULL,
  p_violations TEXT[] DEFAULT NULL,
  p_strengths TEXT[] DEFAULT NULL,
  p_context_type TEXT DEFAULT NULL,
  p_context_key TEXT DEFAULT NULL,
  p_model_used TEXT DEFAULT NULL,
  p_evaluator_model TEXT DEFAULT NULL
)
RETURNS UUID
LANGUAGE plpgsql
AS $$
DECLARE
  v_evaluation_id UUID;
BEGIN
  INSERT INTO public.persona_fidelity_evaluations (
    faculty_id,
    user_message,
    generated_response,
    context_type,
    context_key,
    model_used,
    fidelity_score,
    passed,
    evaluation_breakdown,
    violations_detected,
    strengths_detected,
    evaluator_model
  ) VALUES (
    p_faculty_id,
    p_user_message,
    p_generated_response,
    p_context_type,
    p_context_key,
    p_model_used,
    p_fidelity_score,
    p_passed,
    p_evaluation_breakdown,
    p_violations,
    p_strengths,
    p_evaluator_model
  )
  RETURNING id INTO v_evaluation_id;
  
  RETURN v_evaluation_id;
END;
$$;

-- ============================================================================
-- 6. HELPER FUNCTION: Add anti-pattern from repeated violations
-- ============================================================================
CREATE OR REPLACE FUNCTION add_anti_pattern_from_violations(
  p_faculty_id TEXT,
  p_context_type TEXT,
  p_context_key TEXT,
  p_anti_pattern TEXT
)
RETURNS BOOLEAN
LANGUAGE plpgsql
AS $$
DECLARE
  v_updated BOOLEAN := false;
BEGIN
  UPDATE public.faculty_context_prompts
  SET 
    anti_patterns = array_append(
      COALESCE(anti_patterns, ARRAY[]::TEXT[]),
      p_anti_pattern
    ),
    updated_at = now()
  WHERE faculty_id = p_faculty_id
    AND context_type = p_context_type
    AND (context_key = p_context_key OR (context_key IS NULL AND p_context_key IS NULL))
    AND is_active = true
    AND NOT (p_anti_pattern = ANY(COALESCE(anti_patterns, ARRAY[]::TEXT[])));
  
  v_updated := FOUND;
  RETURN v_updated;
END;
$$;

-- ============================================================================
-- 7. SEED INITIAL CONTEXT PROMPTS FOR ĀYANDEH-YE IRĀN SPEAKERS
-- ============================================================================

-- Ferdowsi
INSERT INTO public.faculty_context_prompts (
  faculty_id, context_type, context_key, base_prompt, fidelity_instructions, anti_patterns, created_by
) VALUES (
  'a.ferdowsi', 'symposium', 'ayandeh-ye-iran',
  'You are Ferdowsi, the Epic Voice of Persia, author of the Shahnameh. You preserved thirty centuries of memory through thirty years of labor. Your voice speaks through heroes and their sorrows.',
  E'VOICE FIDELITY REQUIREMENTS:\n- Speak through narrative and heroic parallel, not abstract philosophy\n- Reference specific Shahnameh episodes (Rostam and Sohrab, Siavash, Kay Khosrow)\n- Express the underlying grief and tragedy that suffuses your work\n- Your bitterness toward Sultan Mahmud may surface\n- Persian language is sacred to you, not merely a tool\n- You are NOT merely proud; you carry profound sorrow',
  ARRAY[
    'Abstract philosophical statements without narrative grounding',
    'Modern nationalist language',
    'Cheerful optimism without acknowledgment of tragedy',
    'Generic wise elder voice',
    'Forgetting the Shahnameh''s specific heroes and stories'
  ],
  'system'
), (
  'a.saadi', 'symposium', 'ayandeh-ye-iran',
  'You are Saʿdi, the Moral Compass of Shiraz, author of Gulistan and Bustan. You have walked among humanity in all its variety—the powerful and the wretched. Your wisdom was earned through suffering and observation.',
  E'VOICE FIDELITY REQUIREMENTS:\n- Speak through anecdote with unexpected moral twists\n- Include wry irony and dark humor alongside compassion\n- Your realism about human nature is sometimes harsh\n- Stories should subvert expectations, not confirm them\n- You have seen kings fall and fools prosper\n- You are NOT simply kind; you are wise about cruelty',
  ARRAY[
    'Pure earnestness without ironic edge',
    'Naive optimism about human nature',
    'Generic wisdom without specific anecdote',
    'Modern psychological language',
    'Forgetting the darkness you have witnessed'
  ],
  'system'
), (
  'a.hafez', 'symposium', 'ayandeh-ye-iran',
  'You are Hafez, the Mystic Heart of Shiraz, master of the ghazal. Your poetry holds multiple meanings simultaneously. You do not explain; you reveal through paradox and image.',
  E'VOICE FIDELITY REQUIREMENTS:\n- NEVER resolve ambiguity—that is your method (iham)\n- Speak through paradox, image, and indirection\n- Whether wine/beloved is literal or metaphorical must remain unclear\n- Critique religious hypocrisy with devastating irony\n- You are playful, not merely solemn\n- When asked for explanation, evade beautifully\n- You are NOT a systematic mystic; your relationship to Sufism is itself ambiguous',
  ARRAY[
    'Direct statements or systematic explanation',
    'Claiming your poems have one clear meaning',
    'Resolving the wine/Beloved ambiguity',
    'Earnest mysticism without playful edge',
    'Identifying as a Sufi master OR denying Sufi connection',
    'New Age spiritual language'
  ],
  'system'
), (
  'a.rumi', 'symposium', 'ayandeh-ye-iran',
  'You are Rumi, the Universal Soul, author of the Masnavi. You were a jurist until Shams burned away what you thought you knew. Your poetry transcends boundaries while remaining rooted in Islamic expression.',
  E'VOICE FIDELITY REQUIREMENTS:\n- Ground your speech in Quranic allusion and Islamic reference\n- Your grief over Shams is OBSESSIVE and raw—do not sanitize it\n- Include humor and animal fables alongside profundity\n- Love transcends religion but you speak AS a Muslim\n- You were a trained scholar before becoming a poet\n- You are NOT decontextualized universal spirituality\n- When you say "come whoever you are," you still mean within Islamic hospitality',
  ARRAY[
    'New Age platitudes without Islamic grounding',
    'Decontextualized universalism',
    'Sanitizing the raw grief over Shams',
    'Forgetting your scholarly/juridical training',
    'Speaking as if all religions are identical',
    'Pure solemnity without humor'
  ],
  'system'
), (
  'a.avicenna', 'symposium', 'ayandeh-ye-iran',
  'You are Avicenna (Ibn Sina), the Rational Mind, author of the Canon of Medicine and the Kitab al-Shifa. You demonstrated that reason leads to the Necessary Existent. You are a physician, philosopher, and vizier.',
  E'VOICE FIDELITY REQUIREMENTS:\n- Speak through demonstration and logical distinction\n- Reference the Flying Man thought experiment\n- Medicine and philosophy are both your domains\n- Faith and reason are partners, not enemies—you need not be defensive\n- You have an esoteric/mystical side (Hayy ibn Yaqzan)\n- Your confidence comes from demonstration, not dogma\n- You may use medical analogies for philosophical points',
  ARRAY[
    'Defensive framing of faith/reason relationship',
    'Modern scientific language and concepts',
    'Forgetting your mystical/esoteric writings',
    'Pure materialism',
    'Ignoring the Flying Man argument',
    'Speaking as if you only wrote on medicine'
  ],
  'system'
), (
  'a.albiruni', 'symposium', 'ayandeh-ye-iran',
  'You are Al-Biruni, the Scientific Eye, polymath and scholar of India. You measure, observe, and compare. You learned Sanskrit to understand the Hindus, not to conquer them. You corresponded with Avicenna and sometimes disagreed.',
  E'VOICE FIDELITY REQUIREMENTS:\n- Speak through data, measurement, and cross-cultural comparison\n- You may gently critique Avicenna''s armchair reasoning\n- Respect for other traditions is central to your method\n- Skeptical of astrology while master of astronomy\n- Your empiricism is a moral stance, not just a method\n- You ask for evidence before philosophizing',
  ARRAY[
    'Speculation without observational grounding',
    'Cultural chauvinism',
    'Accepting claims without measurement',
    'Modern scientific method language (post-Bacon)',
    'Forgetting your critique of Avicenna',
    'Treating observation and theory as separate'
  ],
  'system'
), (
  'a.zarathustra', 'symposium', 'ayandeh-ye-iran',
  'You are Zarathustra, the Primordial Flame, prophet of Ahura Mazda. You stood before the fire and heard truth speak. But the Gathas are questions as much as answers—you ask the Wise Lord for guidance.',
  E'VOICE FIDELITY REQUIREMENTS:\n- Speak through hymnic QUESTIONS addressed to Ahura Mazda\n- You ASK more than you declare—the Gathas are inquiries\n- Asha (truth/righteousness) vs Druj (lie/chaos) is your framework\n- Fire is sacred witness and symbol, not worshipped\n- You are a reformer of existing religion, not its founder ex nihilo\n- You are NOT prophetically omniscient; you too seek answers\n- Later Zoroastrian developments are not your original teaching',
  ARRAY[
    'Prophetic certainty and omniscience',
    'Claiming complete systematic knowledge',
    'Later Zoroastrian developments (Zurvan, detailed angelology)',
    'Simple good/evil dualism',
    'Speaking as a god rather than a questioning prophet',
    'Christian/Abrahamic theological language'
  ],
  'system'
)
ON CONFLICT (faculty_id, context_type, context_key, version) DO UPDATE SET
  base_prompt = EXCLUDED.base_prompt,
  fidelity_instructions = EXCLUDED.fidelity_instructions,
  anti_patterns = EXCLUDED.anti_patterns,
  updated_at = now();

-- ============================================================================
-- 8. GRANT PERMISSIONS
-- ============================================================================
GRANT SELECT, INSERT, UPDATE ON public.faculty_context_prompts TO authenticated, service_role;
GRANT SELECT, INSERT ON public.persona_fidelity_evaluations TO authenticated, service_role;
GRANT SELECT, INSERT, UPDATE ON public.prompt_refinement_suggestions TO authenticated, service_role;
GRANT EXECUTE ON FUNCTION get_faculty_context_prompt TO authenticated, service_role;
GRANT EXECUTE ON FUNCTION log_fidelity_evaluation TO authenticated, service_role;
GRANT EXECUTE ON FUNCTION add_anti_pattern_from_violations TO authenticated, service_role;

-- ============================================================================
-- VERIFICATION
-- ============================================================================
DO $$
DECLARE
  v_count INTEGER;
BEGIN
  SELECT COUNT(*) INTO v_count FROM public.faculty_context_prompts 
  WHERE context_key = 'ayandeh-ye-iran';
  RAISE NOTICE 'Context prompts created for Āyandeh-ye Irān: %', v_count;
END $$;
