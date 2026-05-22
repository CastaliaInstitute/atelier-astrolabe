-- Set up cron job to poll Matrix every 10 seconds
-- Enable pg_cron extension if not already enabled
CREATE EXTENSION IF NOT EXISTS pg_cron;

-- Enable pg_net extension for HTTP requests
CREATE EXTENSION IF NOT EXISTS pg_net;

-- First, unschedule any existing job
SELECT cron.unschedule('matrix-poll') WHERE EXISTS (
  SELECT 1 FROM cron.job WHERE jobname = 'matrix-poll'
);

-- Schedule new job (runs every 10 seconds)
-- Note: Replace YOUR_SERVICE_ROLE_KEY with actual service role key
SELECT cron.schedule(
  'matrix-poll',
  '*/10 * * * * *', -- Every 10 seconds
  $$
  SELECT net.http_post(
    url := 'https://pilmscrodlitdrygabvo.supabase.co/functions/v1/matrix-poll',
    headers := jsonb_build_object(
      'Content-Type', 'application/json',
      'Authorization', 'Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InBpbG1zY3JvZGxpdGRyeWdhYnZvIiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTc2MjM1MDIxMCwiZXhwIjoyMDc3OTI2MjEwfQ.A9Vz-PsJ8bAevaDGTYXwnXO1gPsphlS2FSYTz8qP4Ig'
    ),
    body := '{}'::jsonb
  ) AS request_id;
  $$
);
