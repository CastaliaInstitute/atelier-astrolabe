-- Ensure chat tables are in supabase_realtime publication for Realtime streams
alter publication supabase_realtime add table public.chat_messages;
alter publication supabase_realtime add table public.chat_reactions;