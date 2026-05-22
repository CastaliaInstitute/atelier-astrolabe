-- Fix Kurt Gödel's name field to separate first name from surname
-- Current: name='Kurt Gödel', surname='godel'
-- Should be: name='Kurt', surname='godel'

UPDATE faculty
SET name = 'Kurt'
WHERE id = 'a.godel'
  AND name = 'Kurt Gödel';
