-- Create ritual invitations table for tea and faculty_club_lunch rituals
-- Allows faculty to invite other faculty and accept/decline invitations

CREATE TABLE IF NOT EXISTS public.ritual_invitations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  inviter_slug text NOT NULL, -- Faculty slug of person sending invitation
  invitee_slug text NOT NULL, -- Faculty slug of person being invited
  ritual_type text NOT NULL CHECK (ritual_type IN ('tea', 'faculty_club_lunch')),
  scheduled_date date NOT NULL, -- Date when the ritual is scheduled
  scheduled_time time, -- Optional specific time
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'accepted', 'declined', 'cancelled')),
  message text, -- Optional message from inviter
  created_at timestamptz DEFAULT now(),
  updated_at timestamptz DEFAULT now(),
  accepted_at timestamptz,
  declined_at timestamptz,
  
  -- Ensure faculty can't invite themselves
  CONSTRAINT no_self_invite CHECK (inviter_slug != invitee_slug),
  
);

-- Indexes for efficient querying
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

-- Function to update updated_at timestamp
CREATE OR REPLACE FUNCTION update_ritual_invitations_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_ritual_invitations_updated_at
  BEFORE UPDATE ON public.ritual_invitations
  FOR EACH ROW
  EXECUTE FUNCTION update_ritual_invitations_updated_at();

-- Function to set accepted_at/declined_at timestamps
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

CREATE TRIGGER set_ritual_invitation_timestamps
  BEFORE UPDATE ON public.ritual_invitations
  FOR EACH ROW
  EXECUTE FUNCTION set_ritual_invitation_timestamps();

-- Enable RLS
ALTER TABLE public.ritual_invitations ENABLE ROW LEVEL SECURITY;

-- RLS Policies
-- Faculty can view invitations they sent or received
CREATE POLICY "Faculty can view own invitations"
  ON public.ritual_invitations
  FOR SELECT
  USING (
    auth.jwt() ->> 'preferred_username' = inviter_slug OR
    auth.jwt() ->> 'preferred_username' = invitee_slug
  );

-- Faculty can create invitations (as inviter)
CREATE POLICY "Faculty can create invitations"
  ON public.ritual_invitations
  FOR INSERT
  WITH CHECK (
    auth.jwt() ->> 'preferred_username' = inviter_slug
  );

-- Invitee can update their own invitations (accept/decline)
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

-- Inviter can cancel their invitations
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

COMMENT ON TABLE public.ritual_invitations IS 
'Invitations for tea and faculty_club_lunch rituals. Faculty can invite other faculty members and they can accept or decline.';

COMMENT ON COLUMN public.ritual_invitations.ritual_type IS 
'Type of ritual: "tea" for one-on-one tea, "faculty_club_lunch" for group lunch (3-5 people)';

COMMENT ON COLUMN public.ritual_invitations.status IS 
'Invitation status: pending (awaiting response), accepted, declined, or cancelled by inviter';
