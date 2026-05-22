-- Create get_agents_for_room function for Matrix message routing
-- Returns faculty agents that should respond to a message in a room

CREATE OR REPLACE FUNCTION public.get_agents_for_room(
  p_room_id text,
  p_message_body text,
  p_sender text
)
RETURNS TABLE (
  faculty_id text,
  slug text,
  display_name text,
  priority integer,
  role text
)
LANGUAGE plpgsql
AS $$
BEGIN
  -- Return faculty members who are members of the room
  RETURN QUERY
  SELECT 
    rfm.faculty_id,
    rfm.faculty_id as slug,
    COALESCE(f.name, rfm.faculty_id) as display_name,
    rfm.priority,
    rfm.role
  FROM room_faculty_membership rfm
  LEFT JOIN faculty f ON f.id = rfm.faculty_id
  WHERE rfm.room_id = p_room_id
  ORDER BY rfm.priority DESC, rfm.faculty_id;
END;
$$;
