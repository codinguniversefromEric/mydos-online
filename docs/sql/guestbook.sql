-- MYDOS 線上留言簿：在 Supabase 的 SQL Editor 貼上整段，按 Run

-- 1. 留言資料表（名字 1–20 字、留言 1–200 字、不能有控制字元）
create table public.guestbook (
  id         bigint generated always as identity primary key,
  created_at timestamptz not null default now(),
  name       text not null check (char_length(name) between 1 and 20 and name !~ '[[:cntrl:]]'),
  message    text not null check (char_length(message) between 1 and 200 and message !~ '[[:cntrl:]]')
);

-- 2. Row Level Security：沒有明確允許的事一律不行（所以不能改、不能刪）
alter table public.guestbook enable row level security;

create policy "Anyone can read the guestbook"
  on public.guestbook for select to anon, authenticated using (true);

create policy "Anyone can sign the guestbook"
  on public.guestbook for insert to anon, authenticated with check (true);

-- 3. 訪客只能填 name 和 message，不能自己指定時間或編號
-- （Supabase 預設會把整張表的權限給 anon，所以先全部收回再只給需要的）
revoke insert, update, delete, truncate on public.guestbook from anon, authenticated;
grant insert (name, message) on public.guestbook to anon, authenticated;
grant select on public.guestbook to anon, authenticated;

-- 4. 防洗版：整個留言簿每分鐘最多 20 則
create or replace function public.guestbook_flood_guard() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if (select count(*) from public.guestbook where created_at > now() - interval '1 minute') >= 20 then
    raise exception 'Guestbook is busy, please try again in a minute';
  end if;
  return new;
end $$;

create trigger guestbook_flood
  before insert on public.guestbook
  for each row execute function public.guestbook_flood_guard();

-- 5. 叫 Supabase 的 API 立刻認得這張新表（不然可能要等一下，期間會回 404）
notify pgrst, 'reload schema';
