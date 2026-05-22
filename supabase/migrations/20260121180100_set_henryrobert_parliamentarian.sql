-- Update a.henryrobert's role to parliamentarian in room_faculty_membership
UPDATE room_faculty_membership 
SET role = 'parliamentarian',
    priority = 100  -- High priority so he speaks early
WHERE room_id = '!PXnmXLlzxpsYHbQidS:matrix.inquiry.institute'
  AND faculty_id = 'a.henryrobert';
