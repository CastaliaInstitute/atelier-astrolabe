-- Ensure get_or_create_conversation function exists
-- This function is used by matrix-bridge to get or create conversations

CREATE OR REPLACE FUNCTION public.get_or_create_conversation(
  p_room_id text,
  p_thread_key text DEFAULT NULL,
  p_faculty_id text DEFAULT NULL
)
RETURNS uuid AS $$
DECLARE
  v_conversation_id uuid;
BEGIN
  -- Try to find existing conversation
  SELECT id INTO v_conversation_id
  FROM public.conversations
  WHERE 
    room_id = p_room_id
    AND (thread_key = p_thread_key OR (thread_key IS NULL AND p_thread_key IS NULL))
    AND (faculty_id = p_faculty_id OR (faculty_id IS NULL AND p_faculty_id IS NULL))
  LIMIT 1;

  -- Create if not found
  IF v_conversation_id IS NULL THEN
    INSERT INTO public.conversations (room_id, thread_key, faculty_id)
    VALUES (p_room_id, p_thread_key, p_faculty_id)
    RETURNING id INTO v_conversation_id;
  END IF;

  RETURN v_conversation_id;
END;
$$ LANGUAGE plpgsql;
