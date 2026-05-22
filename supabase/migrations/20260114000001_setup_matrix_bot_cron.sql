-- Set up cron job to poll Matrix for messages
-- This calls matrix-bot-service every 30 seconds to check for new messages

-- Enable pg_cron extension if not already enabled
CREATE EXTENSION IF NOT EXISTS pg_cron;

-- Schedule matrix-bot-service to run every 30 seconds
-- Note: This requires the Supabase project to have pg_cron enabled
-- You may need to enable it in the Supabase dashboard first

SELECT cron.schedule(
  'matrix-bot-service-poll',
  '*/30 * * * * *', -- Every 30 seconds
  $$
  SELECT
    net.http_post(
      url := 'https://xougqdomkoisrxdnagcj.supabase.co/functions/v1/matrix-bot-service',
      headers := jsonb_build_object(
        'Content-Type', 'application/json',
        'Authorization', 'Bearer ' || current_setting('app.settings.service_role_key', true),
        'apikey', current_setting('app.settings.service_role_key', true)
      ),
      body := '{}'::jsonb
    ) AS request_id;
  $$
);

-- Note: You'll need to set the service_role_key in Supabase settings
-- Or use a different authentication method
