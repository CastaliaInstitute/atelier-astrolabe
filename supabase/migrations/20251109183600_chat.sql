-- Chat schema for realtime messaging

create table if not exists public.chat_rooms (
  id text primary key,
  title text not null,
  created_at timestamptz default now()
);

create table if not exists public.chat_messages (
  id uuid primary key default gen_random_uuid(),
  room_id text not null references public.chat_rooms(id) on delete cascade,
  author_id uuid not null,
  author_name text not null,
  text text not null check (char_length(text) <= 4000),
  created_at timestamptz default now()
);

create table if not exists public.chat_reactions (
  message_id uuid not null references public.chat_messages(id) on delete cascade,
  emoji text not null,
  user_id uuid not null,
  created_at timestamptz default now(),
  primary key (message_id, emoji, user_id)
);

-- seed rooms
insert into public.chat_rooms (id, title)
values ('villa.diodati', 'Villa Diodati · Lake Geneva')
on conflict (id) do nothing;

insert into public.chat_rooms (id, title)
values ('villa.diodati.meta', 'Member Meta Chat · Villa Diodati')
on conflict (id) do nothing;

-- RLS
alter table public.chat_rooms enable row level security;
alter table public.chat_messages enable row level security;
alter table public.chat_reactions enable row level security;

-- Public read
DROP POLICY IF EXISTS "rooms are publicly readable" ON public.chat_rooms;
CREATE POLICY "rooms are publicly readable"
  ON public.chat_rooms FOR SELECT USING (true);

DROP POLICY IF EXISTS "messages are publicly readable" ON public.chat_messages;
CREATE POLICY "messages are publicly readable"
  ON public.chat_messages FOR SELECT USING (true);

DROP POLICY IF EXISTS "reactions are publicly readable" ON public.chat_reactions;
CREATE POLICY "reactions are publicly readable"
  ON public.chat_reactions FOR SELECT USING (true);
