-- Ensure all directors are in room_faculty_membership for Board of Directors room
-- This ensures get_agents_for_room returns directors when custodian speaks

INSERT INTO room_faculty_membership (room_id, faculty_id, role, priority)
SELECT 
  '!PXnmXLlzxpsYHbQidS:matrix.inquiry.institute' as room_id,
  faculty_id,
  'director' as role,
  10 as priority
FROM board_of_directors
WHERE position_type IN ('college', 'heretic')
  AND faculty_id IS NOT NULL
ON CONFLICT (room_id, faculty_id) 
DO UPDATE SET 
  role = EXCLUDED.role,
  priority = EXCLUDED.priority;

-- Also ensure parliamentarian is in the room
INSERT INTO room_faculty_membership (room_id, faculty_id, role, priority)
SELECT 
  '!PXnmXLlzxpsYHbQidS:matrix.inquiry.institute' as room_id,
  faculty_id,
  'parliamentarian' as role,
  5 as priority
FROM board_of_directors
WHERE position_type = 'parliamentarian'
  AND faculty_id IS NOT NULL
ON CONFLICT (room_id, faculty_id) 
DO UPDATE SET 
  role = EXCLUDED.role,
  priority = EXCLUDED.priority;
