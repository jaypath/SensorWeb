-- Admin users who may call enroll-device-admin (enroll devices for any email).
-- Run in SQL Editor after schema.sql / schema_v2.sql.

create table if not exists public.admins (
  user_id    uuid primary key references auth.users (id) on delete cascade,
  note       text null,
  created_at timestamptz not null default now()
);

alter table public.admins enable row level security;

-- Admins can see the admin list; only service role writes (SQL Editor / service).
drop policy if exists admins_select_self on public.admins;
create policy admins_select_self
  on public.admins for select to authenticated
  using (auth.uid() = user_id or exists (
    select 1 from public.admins a where a.user_id = auth.uid()
  ));

grant select on public.admins to authenticated;
grant all on public.admins to service_role;

create or replace function public.is_admin(p_uid uuid)
returns boolean
language sql
stable
security definer
set search_path = public
as $$
  select exists (select 1 from public.admins a where a.user_id = p_uid);
$$;

revoke all on function public.is_admin(uuid) from public;
grant execute on function public.is_admin(uuid) to authenticated, service_role;

-- Seed: your account as admin (safe to re-run)
insert into public.admins (user_id, note)
values ('d1bc1fcf-759f-41ce-9789-d962509665eb', 'primary operator')
on conflict (user_id) do nothing;
