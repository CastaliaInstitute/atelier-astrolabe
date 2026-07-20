-- Short device IDs for radar / family lookup.
--
-- Full MAC addresses remain service-role-only. Public-facing firmware/UI paths
-- should use short_id where possible and resolve labels through an Edge Function.

alter table public.astrolabe_devices
  add column if not exists short_id text;

alter table public.astrolabe_devices
  add column if not exists kind text not null default 'astrolabe';

alter table public.astrolabe_devices
  add constraint astrolabe_devices_short_id_format
  check (short_id is null or short_id ~ '^[0-9a-f]{4}$');

alter table public.astrolabe_devices
  add constraint astrolabe_devices_kind_nonempty
  check (length(kind) > 0);

create unique index if not exists astrolabe_devices_channel_short_id_idx
  on public.astrolabe_devices (channel, short_id)
  where short_id is not null;

comment on column public.astrolabe_devices.short_id is
  'Four-hex privacy-preserving display/lookup ID derived from the BLE address bytes used by firmware.';

comment on column public.astrolabe_devices.kind is
  'Sanitized device kind returned by short-ID lookup, for example astrolabe.';
