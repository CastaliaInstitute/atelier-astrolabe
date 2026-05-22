-- ============================================================
-- Parent concept alerts: flag concepts and get notified when
-- the child discusses them (chat or SAMWISE).
-- ============================================================

-- ============================================================
-- 1. Parent concept alerts (one row per concept per child)
-- ============================================================
CREATE TABLE IF NOT EXISTS public.parent_concept_alerts (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),

  caregiver_id uuid NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,
  child_user_id uuid NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,

  -- Phrase to look for in the child's conversations (literal substring match, case-insensitive)
  phrase text NOT NULL CHECK (char_length(trim(phrase)) >= 2),

  -- Optional label for display (e.g. "Bullying" when phrase is "being mean")
  label text,

  notify_email boolean DEFAULT true,
  notify_in_app boolean DEFAULT true,

  created_at timestamptz DEFAULT now(),

  -- One alert per (caregiver, child, phrase) to avoid duplicates
  UNIQUE(caregiver_id, child_user_id, lower(trim(phrase)))
);

CREATE INDEX IF NOT EXISTS idx_parent_concept_alerts_child
  ON public.parent_concept_alerts(child_user_id);
CREATE INDEX IF NOT EXISTS idx_parent_concept_alerts_caregiver
  ON public.parent_concept_alerts(caregiver_id);

-- ============================================================
-- 2. Concept alert notifications (when a match is found)
-- ============================================================
CREATE TABLE IF NOT EXISTS public.concept_alert_notifications (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),

  caregiver_id uuid NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,
  child_user_id uuid NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,
  concept_alert_id uuid NOT NULL REFERENCES public.parent_concept_alerts(id) ON DELETE CASCADE,

  source_type text NOT NULL CHECK (source_type IN ('chat', 'samwise')),
  source_id uuid NOT NULL,
  excerpt text NOT NULL,

  created_at timestamptz DEFAULT now(),
  email_sent_at timestamptz,
  in_app_read_at timestamptz
);

CREATE INDEX IF NOT EXISTS idx_concept_alert_notifications_caregiver
  ON public.concept_alert_notifications(caregiver_id);
CREATE INDEX IF NOT EXISTS idx_concept_alert_notifications_created
  ON public.concept_alert_notifications(created_at DESC);

-- ============================================================
-- 3. RLS
-- ============================================================
ALTER TABLE public.parent_concept_alerts ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.concept_alert_notifications ENABLE ROW LEVEL SECURITY;

-- Caregivers manage their own concept alerts for linked children
CREATE POLICY "Caregivers manage own concept alerts"
  ON public.parent_concept_alerts FOR ALL
  USING (
    caregiver_id = auth.uid()
    AND EXISTS (
      SELECT 1 FROM hs_family_links fl
      WHERE fl.caregiver_id = auth.uid()
        AND fl.student_id = child_user_id
        AND fl.status = 'active'
    )
  )
  WITH CHECK (
    caregiver_id = auth.uid()
    AND EXISTS (
      SELECT 1 FROM hs_family_links fl
      WHERE fl.caregiver_id = auth.uid()
        AND fl.student_id = child_user_id
        AND fl.status = 'active'
    )
  );

-- Caregivers can read and update (mark read) their notifications
CREATE POLICY "Caregivers manage own concept notifications"
  ON public.concept_alert_notifications FOR ALL
  USING (caregiver_id = auth.uid())
  WITH CHECK (caregiver_id = auth.uid());

-- Service role can insert (triggers) and full access
CREATE POLICY "Service role full access concept alerts"
  ON public.parent_concept_alerts FOR ALL USING (auth.role() = 'service_role');
CREATE POLICY "Service role full access concept notifications"
  ON public.concept_alert_notifications FOR ALL USING (auth.role() = 'service_role');

-- ============================================================
-- 4. Match function: check text against a child's alert phrases
-- ============================================================
CREATE OR REPLACE FUNCTION public.check_concept_alerts_for_text(
  p_child_user_id uuid,
  p_text text,
  p_source_type text,
  p_source_id uuid,
  p_excerpt text
)
RETURNS void
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
  r RECORD;
  v_phrase_escaped text;
BEGIN
  IF p_text IS NULL OR trim(p_text) = '' THEN
    RETURN;
  END IF;

  FOR r IN
    SELECT id, caregiver_id, phrase, label
    FROM parent_concept_alerts
    WHERE child_user_id = p_child_user_id
      AND (notify_email OR notify_in_app)
      AND length(trim(phrase)) >= 2
  LOOP
    IF position(lower(trim(r.phrase)) IN lower(p_text)) > 0 THEN
      INSERT INTO concept_alert_notifications (
        caregiver_id, child_user_id, concept_alert_id,
        source_type, source_id, excerpt
      )
      VALUES (
        r.caregiver_id, p_child_user_id, r.id,
        p_source_type, p_source_id, p_excerpt
      );
    END IF;
  END LOOP;
END;
$$;

-- ============================================================
-- 5. Trigger: chat_messages — when a child posts, check concepts
-- ============================================================
CREATE OR REPLACE FUNCTION public.trigger_check_concept_alerts_chat()
RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
BEGIN
  -- Only for users who are linked as students (children)
  IF EXISTS (SELECT 1 FROM hs_family_links fl WHERE fl.student_id = NEW.author_id AND fl.status = 'active') THEN
    PERFORM check_concept_alerts_for_text(
      NEW.author_id,
      NEW.text,
      'chat',
      NEW.id,
      left(NEW.text, 500)
    );
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_chat_messages_concept_alerts ON public.chat_messages;
CREATE TRIGGER trg_chat_messages_concept_alerts
  AFTER INSERT ON public.chat_messages
  FOR EACH ROW
  EXECUTE FUNCTION public.trigger_check_concept_alerts_chat();

-- ============================================================
-- 6. Trigger: hs_samwise_journal — when SAM or child says something
-- ============================================================
CREATE OR REPLACE FUNCTION public.trigger_check_concept_alerts_samwise()
RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
  v_combined text;
BEGIN
  v_combined := coalesce(NEW.sam_said, '') || ' ' || coalesce(NEW.student_response, '');
  IF trim(v_combined) = '' THEN
    RETURN NEW;
  END IF;

  PERFORM check_concept_alerts_for_text(
    NEW.student_id,
    v_combined,
    'samwise',
    NEW.id,
    left(trim(v_combined), 500)
  );
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_samwise_journal_concept_alerts ON hs_samwise_journal;
CREATE TRIGGER trg_samwise_journal_concept_alerts
  AFTER INSERT ON hs_samwise_journal
  FOR EACH ROW
  EXECUTE FUNCTION public.trigger_check_concept_alerts_samwise();

COMMENT ON TABLE public.parent_concept_alerts IS
  'Concepts parents want to be notified about when their child discusses them (chat or SAMWISE).';
COMMENT ON TABLE public.concept_alert_notifications IS
  'Log of when a child conversation matched a parent concept alert; used for in-app list and email delivery.';
