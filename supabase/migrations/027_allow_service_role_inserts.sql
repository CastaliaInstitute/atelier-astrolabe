-- Allow service role to insert works (for Edge Functions)
-- The service role bypasses RLS by default, but let's ensure it's working

-- Add a policy for inserting works (for API/bot usage)
DROP POLICY IF EXISTS "Allow insert for service role" ON works;
CREATE POLICY "Allow insert for service role" ON works
  FOR INSERT
  TO service_role
  WITH CHECK (true);

-- Also allow the authenticated role to insert (in case we use JWT auth later)
DROP POLICY IF EXISTS "Allow authenticated insert" ON works;
CREATE POLICY "Allow authenticated insert" ON works
  FOR INSERT  
  TO authenticated
  WITH CHECK (true);

-- Ensure service role has full access
GRANT ALL ON works TO service_role;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO service_role;
