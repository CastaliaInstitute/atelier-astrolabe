-- Fix faculty display names in Matrix routing function and faculty_voices view
-- Issue: Display names were showing "Douglas Engelbart engelbart" because
-- the function/view was concatenating name + surname, but name already contains the full name

-- ============================================================================
-- 1. Fix get_agents_for_room function
-- ============================================================================
CREATE OR REPLACE FUNCTION public.get_agents_for_room(p_room_id text, p_message_body text DEFAULT NULL)
RETURNS TABLE (
  faculty_id text,
  slug text,
  display_name text,
  priority int,
  role text
)
LANGUAGE plpgsql
AS $$
BEGIN
  RETURN QUERY
  SELECT 
    f.id,
    f.id as slug,
    f.name as display_name,  -- Changed: use name directly, don't append surname
    rfm.priority,
    rfm.role
  FROM public.room_faculty_membership rfm
  JOIN public.faculty f ON f.id = rfm.faculty_id
  JOIN public.matrix_rooms mr ON mr.room_id = rfm.room_id
  WHERE rfm.room_id = p_room_id
    AND f.enabled = true
    AND (
      -- Routing mode: all - all faculty respond
      mr.routing_mode = 'all'
      -- Routing mode: mention - check for mentions
      OR (mr.routing_mode = 'mention' AND (
        p_message_body ILIKE '%@' || COALESCE(f.matrix_user_localpart, f.id) || '%'
        OR p_message_body ILIKE '%@' || f.id || '%'
        OR p_message_body ILIKE '%!' || f.id || '%'
      ))
      -- Routing mode: none - no faculty respond (but we still return the list for UI)
      OR mr.routing_mode = 'none'
    )
  ORDER BY 
    -- Explicit mentions first
    CASE WHEN p_message_body ILIKE '%@' || COALESCE(f.matrix_user_localpart, f.id) || '%' THEN 0 ELSE 1 END,
    rfm.priority DESC,
    f.id;
END;
$$;
