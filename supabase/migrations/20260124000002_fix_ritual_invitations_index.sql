-- Fix missing unique index for ritual_invitations
-- This ensures no duplicate pending invitations

CREATE UNIQUE INDEX IF NOT EXISTS idx_unique_pending_invitation 
ON public.ritual_invitations(inviter_slug, invitee_slug, ritual_type, scheduled_date) 
WHERE status = 'pending';
