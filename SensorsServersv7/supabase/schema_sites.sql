-- Per-user sites (optional grouping). Default slug/name = 'home'.
-- Run in SQL Editor after schema.sql / schema_v2.sql.

create table if not exists public.sites (
  id          uuid primary key default gen_random_uuid(),
  user_id     uuid not null references auth.users (id) on delete cascade,
  slug        text not null,
  name        text not null,
  created_at  timestamptz not null default now(),
  constraint sites_user_slug_unique unique (user_id, slug),
  constraint sites_slug_format check (slug ~ '^[a-z0-9][a-z0-9_-]{0,31}$')
);

create index if not exists sites_user_id_idx on public.sites (user_id);

comment on table public.sites is
  'Logical locations under a user (home, work, office1). Devices belong to one site.';

alter table public.sites enable row level security;

drop policy if exists sites_select_own on public.sites;
create policy sites_select_own
  on public.sites for select to authenticated
  using (auth.uid() = user_id);

drop policy if exists sites_insert_own on public.sites;
create policy sites_insert_own
  on public.sites for insert to authenticated
  with check (auth.uid() = user_id);

drop policy if exists sites_update_own on public.sites;
create policy sites_update_own
  on public.sites for update to authenticated
  using (auth.uid() = user_id)
  with check (auth.uid() = user_id);

drop policy if exists sites_delete_own on public.sites;
create policy sites_delete_own
  on public.sites for delete to authenticated
  using (auth.uid() = user_id);

grant select, insert, update, delete on public.sites to authenticated;
grant all on public.sites to service_role;

-- Normalize slug: lowercase, [a-z0-9_-], default 'home'
create or replace function public.normalize_site_slug(p_slug text)
returns text
language plpgsql
immutable
as $$
declare
  s text;
begin
  s := lower(trim(coalesce(p_slug, '')));
  s := regexp_replace(s, '[^a-z0-9_-]', '', 'g');
  s := regexp_replace(s, '^[^a-z0-9]+', '', 'g');
  if s is null or s = '' then
    s := 'home';
  end if;
  if char_length(s) > 32 then
    s := left(s, 32);
  end if;
  return s;
end;
$$;

revoke all on function public.normalize_site_slug(text) from public;
grant execute on function public.normalize_site_slug(text) to authenticated, service_role;

-- Ensure a site exists for user; returns site id. Default slug/name = home.
create or replace function public.ensure_site(
  p_user_id uuid,
  p_slug text default 'home',
  p_name text default null
)
returns uuid
language plpgsql
security definer
set search_path = public
as $$
declare
  v_slug text := public.normalize_site_slug(p_slug);
  v_name text := nullif(trim(coalesce(p_name, '')), '');
  v_id uuid;
begin
  if v_name is null then
    v_name := v_slug;
  end if;

  select id into v_id
  from public.sites
  where user_id = p_user_id and slug = v_slug;

  if v_id is null then
    insert into public.sites (user_id, slug, name)
    values (p_user_id, v_slug, v_name)
    on conflict (user_id, slug) do update set name = excluded.name
    returning id into v_id;
  end if;

  return v_id;
end;
$$;

revoke all on function public.ensure_site(uuid, text, text) from public;
grant execute on function public.ensure_site(uuid, text, text) to authenticated, service_role;

-- Attach site_id to devices
alter table public.devices
  add column if not exists site_id uuid null references public.sites (id) on delete set null;

create index if not exists devices_user_site_idx on public.devices (user_id, site_id);

-- Backfill: every existing device owner gets 'home'; devices without site_id → home
do $$
declare
  r record;
  sid uuid;
begin
  for r in select distinct user_id from public.devices loop
    sid := public.ensure_site(r.user_id, 'home', 'home');
    update public.devices
    set site_id = sid
    where user_id = r.user_id and site_id is null;
  end loop;
end $$;

-- Staging table optional site slug (resolved at provision time into devices.site_id)
alter table public.device_provisioning
  add column if not exists site_slug text null,
  add column if not exists site_id uuid null references public.sites (id) on delete set null;

-- Refresh devices_safe view to include site_id
create or replace view public.devices_safe
with (security_invoker = true)
as
select
  id,
  user_id,
  device_mac,
  api_key_prefix,
  name,
  is_active,
  created_at,
  last_seen_at,
  device_ip,
  dev_type,
  feature_mask,
  dev_name,
  sending_int,
  firmware_major,
  firmware_minor,
  firmware_patch,
  expired,
  flags,
  data_received,
  data_sent,
  site_id
from public.devices;

grant select on public.devices_safe to authenticated;

grant select (
  id, user_id, device_mac, api_key_prefix, name, is_active, created_at, last_seen_at,
  device_ip, dev_type, feature_mask, dev_name, sending_int,
  firmware_major, firmware_minor, firmware_patch, expired, flags,
  data_received, data_sent, site_id
) on public.devices to authenticated;

grant update (
  name, is_active, device_ip, dev_type, feature_mask, dev_name, sending_int,
  firmware_major, firmware_minor, firmware_patch, expired, flags,
  data_received, data_sent, last_seen_at, site_id
) on public.devices to authenticated;
