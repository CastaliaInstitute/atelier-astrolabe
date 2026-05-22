-- Ephemeral rows: phone completes Google OAuth (PKCE) on Edge-hosted page, then posts tokens;
-- Mynah device polls until it receives the session once (status -> consumed).

create table if not exists public.mynah_device_pairings (
  id uuid primary key,
  secret_hash text not null,
  access_token text,
  refresh_token text,
  expires_at_ms bigint,
  status text not null default 'pending',
  created_at timestamptz not null default now(),
  constraint mynah_device_pairings_status_chk check (status in ('pending', 'ready', 'consumed'))
);

create index if not exists mynah_device_pairings_pending_created
  on public.mynah_device_pairings (created_at)
  where status = 'pending';

alter table public.mynah_device_pairings enable row level security;
