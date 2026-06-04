alter table public.faculty
  add column if not exists line_bust_path text;

comment on column public.faculty.line_bust_path is
  'Supabase Storage object path for a cached monochrome line-art faculty bust, generated lazily by faculty-bust or curated manually.';
