-- Site chrome + default profile UI strings for the faculty SPA (faculty.castalia.institute).
-- Edit the `default` row to change copy without redeploying static assets.

create table if not exists public.faculty_site_template (
  id text primary key default 'default' check (id = 'default'),
  site_kicker text not null default 'Castalia Institute',
  directory_title text not null default 'Faculty',
  directory_lede text not null default 'Browse by college or open a profile.',
  directory_empty_message text not null default 'No active faculty records yet.',
  uncategorized_college_label text not null default 'Institute',
  profile_document_suffix text not null default 'Faculty',
  loading_directory_text text not null default 'Loading directory…',
  loading_profile_text text not null default 'Loading…',
  profile_labels jsonb not null default '{}'::jsonb,
  updated_at timestamptz not null default now()
);

comment on table public.faculty_site_template is
  'Singleton shell copy for the faculty Pages SPA; profile_labels overrides UI strings (merge with app defaults).';

alter table public.faculty_site_template enable row level security;

drop policy if exists faculty_site_template_select_public on public.faculty_site_template;

create policy faculty_site_template_select_public
  on public.faculty_site_template
  for select
  to anon, authenticated
  using (true);

insert into public.faculty_site_template (
  id,
  site_kicker,
  directory_title,
  directory_lede,
  directory_empty_message,
  uncategorized_college_label,
  profile_document_suffix,
  loading_directory_text,
  loading_profile_text,
  profile_labels
)
values (
  'default',
  'Castalia Institute',
  'Faculty',
  'Browse by college or open a profile.',
  'No active faculty records yet.',
  'Institute',
  'Faculty',
  'Loading directory…',
  'Loading…',
  '{}'::jsonb
)
on conflict (id) do nothing;
