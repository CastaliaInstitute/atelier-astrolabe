-- Printable 3D bust mesh (STL) — URL typically points at Supabase Storage `busts` bucket.

alter table public.faculty add column if not exists bust_3d_url text;

comment on column public.faculty.bust_3d_url is
  'Public URL for the faculty 3D bust mesh (e.g. STL in storage bucket busts). Populated by publish/export tooling.';
