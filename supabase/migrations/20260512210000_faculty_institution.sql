-- Directory grid groups by college label; SPA selects this column.
alter table public.faculty
  add column if not exists institution text;

comment on column public.faculty.institution is 'Optional college / affiliation line for directory grouping (nullable).';
