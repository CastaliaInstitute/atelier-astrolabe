-- Vector RAG over durable faculty memories generated from Commonplace dreams.

create extension if not exists vector;

create table if not exists public.faculty_memory_embeddings (
  id uuid primary key default gen_random_uuid(),
  work_id uuid not null references public.works(id) on delete cascade,
  actor_scope text not null,
  actor_email text,
  faculty_slug text not null,
  content_text text not null,
  embedding vector(1536) not null,
  embedding_model text not null default 'gemini-embedding-001',
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (work_id)
);

comment on table public.faculty_memory_embeddings is
  'Gemini embeddings for user/faculty Commonplace memory notes used by ask-faculty vector RAG.';
comment on column public.faculty_memory_embeddings.actor_scope is
  'Memory owner scope. Authenticated users use Account:<email>; unsigned device sessions use Session.';
comment on column public.faculty_memory_embeddings.content_text is
  'Compact memory text embedded for retrieval.';
comment on column public.faculty_memory_embeddings.embedding is
  'Gemini gemini-embedding-001 retrieval document embedding, 1536 dimensions.';

create index if not exists idx_faculty_memory_embeddings_scope_faculty
  on public.faculty_memory_embeddings (actor_scope, faculty_slug, created_at desc);

do $$
begin
  if exists (
    select 1 from public.faculty_memory_embeddings
    where embedding is not null
    limit 1
  ) then
    create index if not exists idx_faculty_memory_embeddings_vector
      on public.faculty_memory_embeddings
      using ivfflat (embedding vector_cosine_ops)
      with (lists = 16);
  end if;
exception
  when others then
    raise notice 'faculty_memory_embeddings vector index deferred: %', sqlerrm;
end $$;

create or replace function public.touch_faculty_memory_embeddings_updated_at()
returns trigger
language plpgsql
as $$
begin
  new.updated_at = now();
  return new;
end;
$$;

drop trigger if exists faculty_memory_embeddings_updated_at on public.faculty_memory_embeddings;
create trigger faculty_memory_embeddings_updated_at
before update on public.faculty_memory_embeddings
for each row
execute function public.touch_faculty_memory_embeddings_updated_at();

alter table public.faculty_memory_embeddings enable row level security;

drop policy if exists "faculty memory embeddings hidden from clients" on public.faculty_memory_embeddings;
create policy "faculty memory embeddings hidden from clients"
  on public.faculty_memory_embeddings
  for all
  using (false)
  with check (false);

create or replace function public.search_faculty_memory_embeddings(
  query_embedding vector(1536),
  filter_actor_scope text,
  filter_faculty_slug text,
  match_count int default 5,
  match_threshold float default 0.35
)
returns table (
  work_id uuid,
  title text,
  abstract text,
  content_md text,
  similarity float,
  created_at timestamptz
)
language sql
stable
as $$
  select
    e.work_id,
    w.title,
    w.abstract,
    w.content_md,
    (1 - (e.embedding <=> query_embedding))::float as similarity,
    e.created_at
  from public.faculty_memory_embeddings e
  join public.works w on w.id = e.work_id
  where e.actor_scope = filter_actor_scope
    and e.faculty_slug = filter_faculty_slug
    and e.embedding is not null
    and (1 - (e.embedding <=> query_embedding)) >= match_threshold
  order by e.embedding <=> query_embedding
  limit greatest(1, least(match_count, 20));
$$;

comment on function public.search_faculty_memory_embeddings(vector, text, text, int, float) is
  'Semantic search over per-user/per-session faculty memory embeddings for ask-faculty RAG.';

grant execute on function public.search_faculty_memory_embeddings(vector, text, text, int, float)
  to anon, authenticated, service_role;
