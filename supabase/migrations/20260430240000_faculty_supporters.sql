-- Open Collective–backed supporter ledger per faculty agent (amounts in USD by default).
-- Populate via Edge Function `opencollective-supporters-webhook`, manual SQL, or sync jobs.

create table if not exists public.faculty_supporters (
  id uuid primary key default gen_random_uuid(),
  faculty_id text not null references public.faculty (id) on delete cascade,
  display_name text not null,
  amount_usd numeric(14, 2) not null check (amount_usd >= 0),
  currency text not null default 'USD',
  contributed_at timestamptz not null default now(),
  source text not null default 'opencollective',
  external_ref text,
  oc_collective_slug text,
  oc_initiative_slug text,
  metadata jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now()
);

comment on table public.faculty_supporters is
  'Public-facing supporter contributions linked to faculty agents; typically filled from Open Collective webhooks or imports.';

create unique index if not exists faculty_supporters_faculty_external_ref_uq
  on public.faculty_supporters (faculty_id, external_ref)
  where external_ref is not null;

create index if not exists faculty_supporters_faculty_id_contributed_at_idx
  on public.faculty_supporters (faculty_id, contributed_at desc);

alter table public.faculty_supporters enable row level security;

drop policy if exists faculty_supporters_select_public on public.faculty_supporters;

create policy faculty_supporters_select_public
  on public.faculty_supporters
  for select
  to anon, authenticated
  using (
    exists (
      select 1
      from public.faculty f
      where f.id = faculty_supporters.faculty_id
        and coalesce(f.is_active, true)
    )
  );

-- Writes are intended for service role (Edge webhook / scripts), not anonymous clients.
