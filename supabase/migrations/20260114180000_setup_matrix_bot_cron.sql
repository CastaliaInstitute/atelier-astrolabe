-- Setup pg_cron to trigger matrix-bot-service every 2 minutes
-- This ensures the bot polls Matrix for new messages automatically

-- Enable required extensions
CREATE EXTENSION IF NOT EXISTS pg_cron;
CREATE EXTENSION IF NOT EXISTS pg_net;

-- Remove existing job if it exists
DO $$
BEGIN
  PERFORM cron.unschedule('matrix-bot-poll');
EXCEPTION WHEN OTHERS THEN
  NULL; -- Job doesn't exist, that's fine
END $$;

-- Create cron job to poll Matrix every 2 minutes
-- Uses pg_net to call the edge function
SELECT cron.schedule(
  'matrix-bot-poll',
  '*/2 * * * *',  -- Every 2 minutes
  $$
  SELECT net.http_post(
    url := 'https://xougqdomkoisrxdnagcj.supabase.co/functions/v1/matrix-bot-service',
    headers := '{"Content-Type": "application/json"}'::jsonb,
    body := '{"trigger": "cron"}'::jsonb
  );
  $$
);

-- Verify job was created
DO $$
DECLARE
  job_count INTEGER;
BEGIN
  SELECT COUNT(*) INTO job_count FROM cron.job WHERE jobname = 'matrix-bot-poll';
  IF job_count > 0 THEN
    RAISE NOTICE 'Matrix bot cron job created successfully';
  ELSE
    RAISE WARNING 'Failed to create matrix bot cron job';
  END IF;
END $$;
