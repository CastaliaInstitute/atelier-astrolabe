-- ============================================================
-- Caregiver full access to child conversations
--
-- Parents (caregivers) must be able to see all conversations their
-- children participate in. This migration adds explicit RLS so that
-- remains true even if other read policies are tightened later.
--
-- Already in place:
--   - hs_samwise_journal / hs_samwise_sessions: "Caregivers view linked
--     student journal" and "Caregivers view linked student sessions".
-- ============================================================

-- ============================================================
-- 1. chat_messages — caregivers can read any message authored by a linked child
-- ============================================================
-- Keeps full access even if "messages are publicly readable" is later
-- replaced with a stricter policy (e.g. members can only read in rooms they're in).

DROP POLICY IF EXISTS "Caregivers read linked children chat messages" ON public.chat_messages;
CREATE POLICY "Caregivers read linked children chat messages"
  ON public.chat_messages FOR SELECT
  TO authenticated
  USING (
    author_id IN (
      SELECT fl.student_id
      FROM hs_family_links fl
      WHERE fl.caregiver_id = auth.uid()
        AND fl.status = 'active'
    )
  );

-- ============================================================
-- 2. chat_reactions — caregivers can read reactions on their child's messages
-- ============================================================
-- So parents can see who reacted to their child's messages.

DROP POLICY IF EXISTS "Caregivers read reactions on linked children messages" ON public.chat_reactions;
CREATE POLICY "Caregivers read reactions on linked children messages"
  ON public.chat_reactions FOR SELECT
  TO authenticated
  USING (
    message_id IN (
      SELECT cm.id
      FROM public.chat_messages cm
      JOIN hs_family_links fl ON fl.student_id = cm.author_id AND fl.caregiver_id = auth.uid() AND fl.status = 'active'
      WHERE cm.id = chat_reactions.message_id
    )
  );

COMMENT ON POLICY "Caregivers read linked children chat messages" ON public.chat_messages IS
  'Parents can read all chat messages authored by their linked children.';
COMMENT ON POLICY "Caregivers read reactions on linked children messages" ON public.chat_reactions IS
  'Parents can read reactions on messages authored by their linked children.';
