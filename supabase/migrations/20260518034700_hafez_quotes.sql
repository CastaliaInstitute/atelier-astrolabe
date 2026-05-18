create table if not exists public.hafez_quotes (
  id bigserial primary key,
  locale text not null default 'en',
  source text not null default 'Hafez',
  quote_text text not null,
  art_prompt text not null,
  art_seed bigint,
  palette text,
  active boolean not null default true,
  starts_on date,
  ends_on date,
  sort_order integer not null default 0,
  created_at timestamptz not null default timezone('utc', now()),
  updated_at timestamptz not null default timezone('utc', now())
);

create unique index if not exists hafez_quotes_locale_quote_uidx
  on public.hafez_quotes (locale, quote_text);

create index if not exists hafez_quotes_active_locale_idx
  on public.hafez_quotes (active, locale);

create or replace function public.hafez_quotes_set_updated_at()
returns trigger
language plpgsql
as $$
begin
  new.updated_at = timezone('utc', now());
  return new;
end;
$$;

drop trigger if exists trg_hafez_quotes_updated_at on public.hafez_quotes;
create trigger trg_hafez_quotes_updated_at
before update on public.hafez_quotes
for each row execute function public.hafez_quotes_set_updated_at();

alter table public.hafez_quotes enable row level security;

drop policy if exists "hafez_quotes_select_public" on public.hafez_quotes;
create policy "hafez_quotes_select_public"
on public.hafez_quotes
for select
to anon, authenticated
using (
  active
  and (starts_on is null or starts_on <= current_date)
  and (ends_on is null or ends_on >= current_date)
);

create or replace function public.pick_hafez_quote(
  p_day date default current_date,
  p_locale text default 'en'
)
returns table(
  quote_text text,
  source text,
  art_prompt text,
  art_seed bigint,
  palette text
)
language sql
stable
set search_path = public
as $$
  with candidates as (
    select
      h.id,
      h.quote_text,
      h.source,
      h.art_prompt,
      coalesce(
        h.art_seed,
        to_number(substr(md5(h.quote_text || '|' || p_day::text), 1, 8), 'XXXXXXXX')::bigint
      ) as art_seed,
      coalesce(h.palette, '') as palette,
      h.sort_order
    from public.hafez_quotes h
    where h.active
      and h.locale = p_locale
      and (h.starts_on is null or h.starts_on <= p_day)
      and (h.ends_on is null or h.ends_on >= p_day)
  )
  select
    c.quote_text,
    c.source,
    c.art_prompt,
    c.art_seed,
    c.palette
  from candidates c
  order by c.sort_order asc, md5(c.id::text || '|' || p_day::text)
  limit 1;
$$;

grant execute on function public.pick_hafez_quote(date, text)
  to anon, authenticated, service_role;

insert into public.hafez_quotes (locale, source, quote_text, art_prompt, art_seed, palette, sort_order)
values
  (
    'en',
    'Hafez',
    'I wish I could show you, when you are lonely or in darkness, the astonishing light of your own being.',
    'A luminous midnight garden, calligraphic wind, moonlit cypress silhouettes, glowing particles.',
    130913,
    'midnight-indigo',
    10
  ),
  (
    'en',
    'Hafez',
    'Fear is the cheapest room in the house. I would like to see you living in better conditions.',
    'Abstract doorway opening from shadow into saffron and lapis light, brushstroke textures.',
    220715,
    'saffron-cobalt',
    20
  ),
  (
    'en',
    'Hafez',
    'Even after all this time, the sun never says to the earth, "You owe me." Look what happens with a love like that.',
    'Sun and earth in devotional orbit, soft gold rays, deep ultramarine atmosphere.',
    998877,
    'rose-gold',
    30
  ),
  (
    'en',
    'Hafez',
    'I am in love with every church, and mosque, and temple, and any kind of shrine because I know it is there that people say the different names of the one God.',
    'Geometric sacred architecture blending into one horizon, jewel-tone mosaics, starry dusk.',
    447733,
    'plum-amber',
    40
  ),
  (
    'en',
    'Hafez',
    'Our union is like this: You feel cold so I reach for a blanket to cover our shivering feet.',
    'Two figures under one woven blanket beside a lantern, intimate warm night palette.',
    556621,
    'seafoam-obsidian',
    50
  )
on conflict (locale, quote_text) do nothing;
