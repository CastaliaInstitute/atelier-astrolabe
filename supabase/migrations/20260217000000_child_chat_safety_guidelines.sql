-- ============================================================
-- Parent-child chat safety guidelines
--
-- Formal parent-child relationships are represented by hs_family_links
-- (caregiver_id = parent, student_id = child). This migration adds
-- parent-configurable safety guidelines that apply when a child
-- participates in chats (chat_rooms, ask-faculty, project-chat, etc.).
-- ============================================================

-- ============================================================
-- 1. Add manage_chat_safety_guidelines to default permissions
-- ============================================================
-- New family links get this permission. Existing rows: key may be missing
-- (RLS treats missing as allowed so caregivers can manage guidelines).

ALTER TABLE hs_family_links
  ALTER COLUMN permissions SET DEFAULT '{
    "view_progress": true,
    "view_aegis_observations": true,
    "manage_privacy_settings": true,
    "manage_curriculum": true,
    "communicate_with_mentor": true,
    "manage_samwise_preferences": true,
    "manage_chat_safety_guidelines": true
  }'::jsonb;

COMMENT ON COLUMN hs_family_links.permissions IS
  'JSON object of permission flags. manage_chat_safety_guidelines allows the caregiver to set chat safety guidelines for this child.';

-- ============================================================
-- 2. Child chat safety guidelines table
-- ============================================================
CREATE TABLE IF NOT EXISTS public.child_chat_safety_guidelines (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),

  -- The child (student) user_id — must be linked via hs_family_links
  child_user_id uuid NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,

  -- Set by this caregiver (must be an active caregiver for child_user_id)
  set_by_caregiver_id uuid NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,

  -- Allowed chat room ids (public.chat_rooms.id). Empty array = none; NULL = no restriction (all allowed).
  allowed_chat_room_ids text[],

  -- Allowed faculty ids for ask-faculty / faculty chats. Empty array = none; NULL = no restriction.
  allowed_faculty_ids text[],

  -- Optional time windows when the child may chat. JSON array of { "start": "HH:MM", "end": "HH:MM" } in family timezone.
  -- NULL = no time restriction.
  time_windows jsonb,

  -- If true, responses must pass an additional content filter / child-safe tone.
  require_content_filter boolean DEFAULT true,

  -- Optional custom instructions injected into the system prompt when this child is in the conversation
  -- (e.g. "Avoid graphic violence; keep explanations age-appropriate for under 10.")
  custom_instructions text,

  -- Topic restrictions: optional list of topics to avoid or to limit. Format: ["topic1", "topic2"] or JSON object.
  topic_restrictions jsonb,

  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),

  UNIQUE(child_user_id)
);

CREATE INDEX IF NOT EXISTS idx_child_chat_safety_guidelines_child
  ON public.child_chat_safety_guidelines(child_user_id);
CREATE INDEX IF NOT EXISTS idx_child_chat_safety_guidelines_caregiver
  ON public.child_chat_safety_guidelines(set_by_caregiver_id);

COMMENT ON TABLE public.child_chat_safety_guidelines IS
  'Parent-defined safety rules for a child''s participation in chats (room chat, ask-faculty, project-chat). One row per child; primary caregiver wins if multiple caregivers exist.';

-- ============================================================
-- 3. RLS
-- ============================================================
ALTER TABLE public.child_chat_safety_guidelines ENABLE ROW LEVEL SECURITY;

-- Caregivers with active link can manage guidelines (manage_chat_safety_guidelines permission; missing key = allowed)
CREATE POLICY "Caregivers manage guidelines for linked children"
  ON public.child_chat_safety_guidelines FOR ALL
  USING (
    set_by_caregiver_id = auth.uid()
    AND EXISTS (
      SELECT 1 FROM hs_family_links fl
      WHERE fl.caregiver_id = auth.uid()
        AND fl.student_id = child_chat_safety_guidelines.child_user_id
        AND fl.status = 'active'
        AND (fl.permissions->>'manage_chat_safety_guidelines' IS NULL OR (fl.permissions->>'manage_chat_safety_guidelines')::boolean = true)
    )
  )
  WITH CHECK (
    set_by_caregiver_id = auth.uid()
    AND EXISTS (
      SELECT 1 FROM hs_family_links fl
      WHERE fl.caregiver_id = auth.uid()
        AND fl.student_id = child_chat_safety_guidelines.child_user_id
        AND fl.status = 'active'
        AND (fl.permissions->>'manage_chat_safety_guidelines' IS NULL OR (fl.permissions->>'manage_chat_safety_guidelines')::boolean = true)
    )
  );

-- Child can read their own guidelines (so the client can enforce allowlists)
CREATE POLICY "Children can read own guidelines"
  ON public.child_chat_safety_guidelines FOR SELECT
  USING (child_user_id = auth.uid());

-- Service role full access
CREATE POLICY "Service role full access to child_chat_safety_guidelines"
  ON public.child_chat_safety_guidelines FOR ALL
  USING (auth.role() = 'service_role');

-- ============================================================
-- 4. Helper: get effective safety guidelines for a child user
-- ============================================================
CREATE OR REPLACE FUNCTION public.get_child_chat_safety_guidelines(p_child_user_id uuid)
RETURNS public.child_chat_safety_guidelines
LANGUAGE sql
STABLE
SECURITY DEFINER
SET search_path = public
AS $$
  SELECT g.*
  FROM child_chat_safety_guidelines g
  JOIN hs_family_links fl ON fl.student_id = g.child_user_id AND fl.caregiver_id = g.set_by_caregiver_id
  WHERE g.child_user_id = p_child_user_id
    AND fl.status = 'active'
  ORDER BY fl.is_primary DESC NULLS LAST
  LIMIT 1;
$$;

COMMENT ON FUNCTION public.get_child_chat_safety_guidelines(uuid) IS
  'Returns the effective chat safety guidelines for a child user. Used by ask-faculty and chat UIs to enforce parent rules.';

-- ============================================================
-- 5. Helper: check if a user is a child with guidelines
-- ============================================================
CREATE OR REPLACE FUNCTION public.is_child_with_guidelines(p_user_id uuid)
RETURNS boolean
LANGUAGE sql
STABLE
SECURITY DEFINER
SET search_path = public
AS $$
  SELECT EXISTS (
    SELECT 1 FROM child_chat_safety_guidelines WHERE child_user_id = p_user_id
  );
$$;
