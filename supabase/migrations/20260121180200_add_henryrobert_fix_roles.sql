-- Add Henry Martyn Robert as parliamentarian
INSERT INTO room_faculty_membership (room_id, faculty_id, role, priority)
VALUES ('!PXnmXLlzxpsYHbQidS:matrix.inquiry.institute', 'a.henryrobert', 'parliamentarian', 100)
ON CONFLICT (room_id, faculty_id) 
DO UPDATE SET role = 'parliamentarian', priority = 100;

-- Fix Diogenes role to heretic
UPDATE room_faculty_membership 
SET role = 'heretic'
WHERE room_id = '!PXnmXLlzxpsYHbQidS:matrix.inquiry.institute'
  AND faculty_id = 'a.diogenes';
