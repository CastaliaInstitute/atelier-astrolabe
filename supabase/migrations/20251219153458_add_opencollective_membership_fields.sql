-- Add OpenCollective fields to memberships table
-- This allows tracking OpenCollective subscriptions alongside Stripe

-- ============================================================================
-- ADD OPENCOLLECTIVE FIELDS
-- ============================================================================

ALTER TABLE public.memberships
ADD COLUMN IF NOT EXISTS opencollective_account_id text,
ADD COLUMN IF NOT EXISTS opencollective_order_id text,
ADD COLUMN IF NOT EXISTS opencollective_subscription_id text,
ADD COLUMN IF NOT EXISTS opencollective_tier_slug text,
ADD COLUMN IF NOT EXISTS payment_provider text DEFAULT 'stripe' CHECK (payment_provider IN ('stripe', 'opencollective'));

-- Add index for OpenCollective lookups
CREATE INDEX IF NOT EXISTS idx_memberships_opencollective_account_id 
ON public.memberships(opencollective_account_id) 
WHERE opencollective_account_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_memberships_opencollective_order_id 
ON public.memberships(opencollective_order_id) 
WHERE opencollective_order_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_memberships_opencollective_subscription_id 
ON public.memberships(opencollective_subscription_id) 
WHERE opencollective_subscription_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_memberships_payment_provider 
ON public.memberships(payment_provider);

-- Update plan_id constraint to include 'alpha'
ALTER TABLE public.memberships
DROP CONSTRAINT IF EXISTS memberships_plan_id_check;

ALTER TABLE public.memberships
ADD CONSTRAINT memberships_plan_id_check 
CHECK (plan_id IN ('monthly', 'annual', 'midwinter', 'alpha'));

-- Update payment_transactions to support OpenCollective
ALTER TABLE public.payment_transactions
DROP CONSTRAINT IF EXISTS payment_transactions_source_check;

ALTER TABLE public.payment_transactions
ADD CONSTRAINT payment_transactions_source_check 
CHECK (source IN ('stripe', 'coinbase', 'opencollective'));

ALTER TABLE public.payment_transactions
ADD COLUMN IF NOT EXISTS opencollective_order_id text,
ADD COLUMN IF NOT EXISTS opencollective_transaction_id text;

CREATE INDEX IF NOT EXISTS idx_payment_transactions_opencollective_order_id 
ON public.payment_transactions(opencollective_order_id) 
WHERE opencollective_order_id IS NOT NULL;

-- Add comments
COMMENT ON COLUMN public.memberships.opencollective_account_id IS 'OpenCollective account ID (user/collective ID)';
COMMENT ON COLUMN public.memberships.opencollective_order_id IS 'OpenCollective order ID for the contribution';
COMMENT ON COLUMN public.memberships.opencollective_subscription_id IS 'OpenCollective subscription/order ID for recurring contributions';
COMMENT ON COLUMN public.memberships.opencollective_tier_slug IS 'OpenCollective tier slug (e.g., alpha-member, monthly-member)';
COMMENT ON COLUMN public.memberships.payment_provider IS 'Payment provider: stripe or opencollective';
