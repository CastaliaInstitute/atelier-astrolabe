-- Tight RLS: only authenticated members can post/react

-- Only authenticated users can insert messages AND must be a member/inquisitor
DROP POLICY IF EXISTS "members can post" ON public.chat_messages;
CREATE POLICY "members can post"
  ON public.chat_messages FOR INSERT
  TO authenticated
  WITH CHECK (
    EXISTS (
      SELECT 1 FROM public.profiles p
      WHERE p.id = auth.uid()
        AND (p.roles && ARRAY['member','inquisitor']::text[])
    )
  );

-- Only authenticated users can insert reactions AND must be a member/inquisitor
DROP POLICY IF EXISTS "members can react" ON public.chat_reactions;
CREATE POLICY "members can react"
  ON public.chat_reactions FOR INSERT
  TO authenticated
  WITH CHECK (
    EXISTS (
      SELECT 1 FROM public.profiles p
      WHERE p.id = auth.uid()
        AND (p.roles && ARRAY['member','inquisitor']::text[])
    )
  );

-- Allow users to remove their own reactions
DROP POLICY IF EXISTS "author can remove reaction" ON public.chat_reactions;
CREATE POLICY "author can remove reaction"
  ON public.chat_reactions FOR DELETE
  TO authenticated
  USING (user_id = auth.uid());
