-- Device provisioning for Astrolabe firmware.
-- Store only for service-role Edge Function access; do not expose this table to browser clients.

create table if not exists public.astrolabe_devices (
  mac text not null,
  channel text not null,
  device_secret text not null,
  owner_user_id uuid references auth.users(id) on delete set null,
  label text,
  enabled boolean not null default true,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  primary key (mac, channel),
  constraint astrolabe_devices_mac_format check (mac ~ '^[0-9a-f]{2}(:[0-9a-f]{2}){5}$'),
  constraint astrolabe_devices_secret_format check (device_secret ~ '^[0-9a-f]{64}$'),
  constraint astrolabe_devices_channel_nonempty check (length(channel) > 0)
);

alter table public.astrolabe_devices enable row level security;

drop policy if exists "astrolabe devices hidden from anon/authenticated" on public.astrolabe_devices;
create policy "astrolabe devices hidden from anon/authenticated"
  on public.astrolabe_devices
  for all
  using (false)
  with check (false);

create or replace function public.touch_astrolabe_devices_updated_at()
returns trigger
language plpgsql
as $$
begin
  new.updated_at = now();
  return new;
end;
$$;

drop trigger if exists astrolabe_devices_updated_at on public.astrolabe_devices;
create trigger astrolabe_devices_updated_at
before update on public.astrolabe_devices
for each row
execute function public.touch_astrolabe_devices_updated_at();
