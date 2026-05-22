-- Add music room for Matrix chat with musical notation features

insert into public.chat_rooms (id, title)
values ('music', 'Music · Musical Notation & Discussion')
on conflict (id) do nothing;
