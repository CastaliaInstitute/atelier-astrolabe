-- Disable the matrix-poll cron job since we're switching to webhooks
-- The webhook bridge will handle real-time event forwarding

-- Unschedule the matrix-poll cron job
SELECT cron.unschedule('matrix-poll') WHERE EXISTS (
  SELECT 1 FROM cron.job WHERE jobname = 'matrix-poll'
);
