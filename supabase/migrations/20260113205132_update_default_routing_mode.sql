-- Update default routing_mode for new matrix_rooms to 'all'
-- This allows faculty to respond to all messages, not just mentions

ALTER TABLE public.matrix_rooms 
  ALTER COLUMN routing_mode SET DEFAULT 'all';

-- Update existing rooms that are still on 'mention' mode to 'all'
UPDATE public.matrix_rooms 
SET routing_mode = 'all' 
WHERE routing_mode = 'mention';
