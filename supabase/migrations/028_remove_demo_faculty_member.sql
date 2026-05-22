-- Remove the demo faculty member and their works
DELETE FROM works WHERE primary_author_id = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890';
DELETE FROM persons WHERE id = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890';
