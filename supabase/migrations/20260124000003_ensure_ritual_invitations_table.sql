-- Ensure ritual_invitations table exists (in case first migration had issues)
-- This is a safety migration to ensure the table is created properly

CREATE TABLE IF NOT EXISTS public.ritual_invitations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  inviter_slug text NOT NULL,
  invitee_slug text NOT NULL,
  ritual_type text NOT NULL CHECK (ritual_type IN ('tea', 'faculty_club_lunch')),
  scheduled_date date NOT NULL,
  scheduled_time time,
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'accepted', 'declined', 'cancelled')),
  message text,
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  accepted_at timestamptz,
  declined_at timestamptz,
  CONSTRAINT no_self_invite CHECK (inviter_slug != invitee_slug)
);

-- Create indexes if they don't exist
CREATE INDEX IF NOT EXISTS idx_ritual_invitations_inviter 
ON public.ritual_invitations(inviter_slug) 
WHERE status IN ('pending', 'accepted');

CREATE INDEX IF NOT EXISTS idx_ritual_invitations_invitee 
ON public.ritual_invitations(invitee_slug) 
WHERE status = 'pending';

CREATE INDEX IF NOT EXISTS idx_ritual_invitations_ritual_date 
ON public.ritual_invitations(ritual_type, scheduled_date, status);

CREATE INDEX IF NOT EXISTS idx_ritual_invitations_status 
ON public.ritual_invitations(status) 
WHERE status = 'pending';

-- Unique index to prevent duplicate pending invitations
CREATE UNIQUE INDEX IF NOT EXISTS idx_unique_pending_invitation 
ON public.ritual_invitations(inviter_slug, invitee_slug, ritual_type, scheduled_date) 
WHERE status = 'pending';

-- Functions
CREATE OR REPLACE FUNCTION update_ritual_invitations_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS update_ritual_invitations_updated_at ON public.ritual_invitations;
CREATE TRIGGER update_ritual_invitations_updated_at
  BEFORE UPDATE ON public.ritual_invitations
  FOR EACH ROW
  EXECUTE FUNCTION update_ritual_invitations_updated_at();

CREATE OR REPLACE FUNCTION set_ritual_invitation_timestamps()
RETURNS TRIGGER AS $$
BEGIN
  IF NEW.status = 'accepted' AND OLD.status != 'accepted' THEN
    NEW.accepted_at = now();
  END IF;
  
  IF NEW.status = 'declined' AND OLD.status != 'declined' THEN
    NEW.declined_at = now();
  END IF;
  
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS set_ritual_invitation_timestamps ON public.ritual_invitations;
CREATE TRIGGER set_ritual_invitation_timestamps
  BEFORE UPDATE ON public.ritual_invitations
  FOR EACH ROW
  EXECUTE FUNCTION set_ritual_invitation_timestamps();

-- Enable RLS
ALTER TABLE public.ritual_invitations ENABLE ROW LEVEL SECURITY;

-- Drop existing policies if they exist and recreate
DROP POLICY IF EXISTS "Faculty can view own invitations" ON public.ritual_invitations;
DROP POLICY IF EXISTS "Faculty can create invitations" ON public.ritual_invitations;
DROP POLICY IF EXISTS "Invitee can update invitations" ON public.ritual_invitations;
DROP POLICY IF EXISTS "Inviter can cancel invitations" ON public.ritual_invitations;

CREATE POLICY "Faculty can view own invitations"
  ON public.ritual_invitations
  FOR SELECT
  USING (
    auth.jwt() ->> 'preferred_username' = inviter_slug OR
    auth.jwt() ->> 'preferred_username' = invitee_slug
  );

CREATE POLICY "Faculty can create invitations"
  ON public.ritual_invitations
  FOR INSERT
  WITH CHECK (
    auth.jwt() ->> 'preferred_username' = inviter_slug
  );

CREATE POLICY "Invitee can update invitations"
  ON public.ritual_invitations
  FOR UPDATE
  USING (
    auth.jwt() ->> 'preferred_username' = invitee_slug
  )
  WITH CHECK (
    auth.jwt() ->> 'preferred_username' = invitee_slug AND
    (status = 'accepted' OR status = 'declined')
  );

CREATE POLICY "Inviter can cancel invitations"
  ON public.ritual_invitations
  FOR UPDATE
  USING (
    auth.jwt() ->> 'preferred_username' = inviter_slug AND
    status = 'pending'
  )
  WITH CHECK (
    auth.jwt() ->> 'preferred_username' = inviter_slug AND
    status = 'cancelled'
  );
