-- Device traffic via PostgREST (no device-api Edge Function).
-- Run after schema.sql, schema_v2.sql, schema_sites.sql.
--
-- Devices still mint JWT via Edge mint-device-jwt / claim via claim-device.
-- All ongoing reads/writes use /rest/v1 + these RPCs under RLS.

-- ---------------------------------------------------------------------------
-- 1) Lock down ensure_site: only service_role may pass arbitrary user_id
-- ---------------------------------------------------------------------------
revoke all on function public.ensure_site(uuid, text, text) from public;
revoke all on function public.ensure_site(uuid, text, text) from authenticated;
grant execute on function public.ensure_site(uuid, text, text) to service_role;

create or replace function public.ensure_my_site(
  p_slug text default 'home',
  p_name text default null
)
returns uuid
language plpgsql
security definer
set search_path = public
as $$
declare
  uid uuid := auth.uid();
begin
  if uid is null then
    raise exception 'not_authenticated' using errcode = '28000';
  end if;
  if not public.user_has_cloud_access(uid) then
    raise exception 'subscription_inactive' using errcode = '42501';
  end if;
  return public.ensure_site(uid, p_slug, p_name);
end;
$$;

revoke all on function public.ensure_my_site(text, text) from public;
grant execute on function public.ensure_my_site(text, text) to authenticated, service_role;

-- ---------------------------------------------------------------------------
-- 2) Delete site with device reassignment (mirrors former device-api delete_site)
-- ---------------------------------------------------------------------------
create or replace function public.delete_my_site(p_slug text)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
  uid uuid := auth.uid();
  v_slug text := public.normalize_site_slug(p_slug);
  victim record;
  target record;
  remaining int;
  moved int := 0;
  created_home boolean := false;
  home_id uuid;
begin
  if uid is null then
    raise exception 'not_authenticated' using errcode = '28000';
  end if;
  if not public.user_has_cloud_access(uid) then
    raise exception 'subscription_inactive' using errcode = '42501';
  end if;

  select id, slug, name into victim
  from public.sites
  where user_id = uid and slug = v_slug;
  if victim.id is null then
    raise exception 'site_not_found' using errcode = 'P0002';
  end if;

  select count(*)::int into remaining
  from public.sites
  where user_id = uid and id <> victim.id;

  if remaining > 0 then
    select id, slug, name into target
    from public.sites
    where user_id = uid and id <> victim.id
    order by case when slug = 'home' then 0 else 1 end, slug
    limit 1;

    update public.devices
    set site_id = target.id
    where user_id = uid and site_id = victim.id;
    get diagnostics moved = row_count;

    delete from public.sites where id = victim.id and user_id = uid;
  else
    update public.devices
    set site_id = null
    where user_id = uid and site_id = victim.id;

    delete from public.sites where id = victim.id and user_id = uid;

    home_id := public.ensure_site(uid, 'home', 'home');
    created_home := true;

    update public.devices
    set site_id = home_id
    where user_id = uid and site_id is null;
    get diagnostics moved = row_count;

    select id, slug, name into target
    from public.sites where id = home_id;
  end if;

  return jsonb_build_object(
    'deleted_slug', victim.slug,
    'target_slug', target.slug,
    'target_id', target.id,
    'devices_moved', moved,
    'created_home', created_home
  );
end;
$$;

revoke all on function public.delete_my_site(text) from public;
grant execute on function public.delete_my_site(text) to authenticated, service_role;

-- ---------------------------------------------------------------------------
-- 3) Insert reading + optional sensors upsert + last_seen (one PostgREST RPC)
-- ---------------------------------------------------------------------------
create or replace function public.insert_my_reading(
  p_device_mac text,
  p_sns_type smallint,
  p_sns_id smallint,
  p_sns_value double precision,
  p_sns_name text default '',
  p_device_ip text default null,
  p_utc_offset integer default 0,
  p_time_logged timestamptz default now(),
  p_time_read timestamptz default null,
  p_flagged boolean default false,
  p_expired boolean default false,
  p_critical boolean default false,
  p_sending_int integer default null,
  p_flags smallint default 0,
  p_refresh_sensor boolean default true
)
returns bigint
language plpgsql
security invoker
set search_path = public
as $$
declare
  uid uuid := auth.uid();
  v_mac text;
  v_id bigint;
  v_ip inet;
begin
  if uid is null then
    raise exception 'not_authenticated' using errcode = '28000';
  end if;
  if not public.user_has_cloud_access(uid) then
    raise exception 'subscription_inactive' using errcode = '42501';
  end if;

  v_mac := upper(regexp_replace(coalesce(p_device_mac, ''), '[^0-9A-Fa-f]', '', 'g'));
  if char_length(v_mac) <> 12 then
    raise exception 'invalid_device_mac' using errcode = '22023';
  end if;
  if not public.is_my_active_device(v_mac) then
    raise exception 'device_not_owned' using errcode = '42501';
  end if;

  begin
    v_ip := nullif(trim(p_device_ip), '')::inet;
  exception when others then
    v_ip := null;
  end;

  insert into public.sensor_readings (
    user_id, device_mac, device_ip, sns_type, sns_id, sns_name, utc_offset,
    time_logged, time_read, flagged, expired, critical, sns_value, sending_int, flags
  ) values (
    uid, v_mac, v_ip, p_sns_type, p_sns_id, coalesce(p_sns_name, ''), coalesce(p_utc_offset, 0),
    coalesce(p_time_logged, now()), p_time_read, coalesce(p_flagged, false),
    coalesce(p_expired, false), coalesce(p_critical, false), p_sns_value, p_sending_int,
    coalesce(p_flags, 0)
  )
  returning id into v_id;

  update public.devices
  set
    last_seen_at = now(),
    device_ip = coalesce(v_ip, device_ip)
  where device_mac = v_mac and user_id = uid;

  if coalesce(p_refresh_sensor, true) then
    insert into public.sensors (
      device_mac, sns_type, sns_id, user_id, sns_name, sns_value,
      time_read, time_logged, sending_int, flags, expired, utc_offset, updated_at
    ) values (
      v_mac, p_sns_type, p_sns_id, uid, coalesce(p_sns_name, ''), p_sns_value,
      p_time_read, coalesce(p_time_logged, now()), coalesce(p_sending_int, 300),
      coalesce(p_flags, 0), coalesce(p_expired, false), coalesce(p_utc_offset, 0), now()
    )
    on conflict (device_mac, sns_type, sns_id) do update set
      sns_name = excluded.sns_name,
      sns_value = excluded.sns_value,
      time_read = excluded.time_read,
      time_logged = excluded.time_logged,
      sending_int = excluded.sending_int,
      flags = excluded.flags,
      expired = excluded.expired,
      utc_offset = excluded.utc_offset,
      updated_at = now();
  end if;

  return v_id;
end;
$$;

revoke all on function public.insert_my_reading(
  text, smallint, smallint, double precision, text, text, integer,
  timestamptz, timestamptz, boolean, boolean, boolean, integer, smallint, boolean
) from public;
grant execute on function public.insert_my_reading(
  text, smallint, smallint, double precision, text, text, integer,
  timestamptz, timestamptz, boolean, boolean, boolean, integer, smallint, boolean
) to authenticated, service_role;

-- ---------------------------------------------------------------------------
-- 4) Subscription gate on writes (reads still RLS-owned)
-- ---------------------------------------------------------------------------
drop policy if exists sensor_readings_insert_own on public.sensor_readings;
create policy sensor_readings_insert_own
  on public.sensor_readings
  for insert
  to authenticated
  with check (
    auth.uid() = user_id
    and public.is_my_active_device(device_mac)
    and public.user_has_cloud_access(auth.uid())
  );

drop policy if exists sensors_insert_own on public.sensors;
create policy sensors_insert_own
  on public.sensors for insert to authenticated
  with check (
    auth.uid() = user_id
    and public.is_my_active_device(device_mac)
    and public.user_has_cloud_access(auth.uid())
  );

drop policy if exists sensors_update_own on public.sensors;
create policy sensors_update_own
  on public.sensors for update to authenticated
  using (auth.uid() = user_id and public.user_has_cloud_access(auth.uid()))
  with check (
    auth.uid() = user_id
    and public.is_my_active_device(device_mac)
    and public.user_has_cloud_access(auth.uid())
  );

drop policy if exists devices_update_own on public.devices;
create policy devices_update_own
  on public.devices
  for update
  to authenticated
  using (auth.uid() = user_id and public.user_has_cloud_access(auth.uid()))
  with check (auth.uid() = user_id);

drop policy if exists sites_insert_own on public.sites;
create policy sites_insert_own
  on public.sites for insert to authenticated
  with check (auth.uid() = user_id and public.user_has_cloud_access(auth.uid()));

drop policy if exists sites_update_own on public.sites;
create policy sites_update_own
  on public.sites for update to authenticated
  using (auth.uid() = user_id and public.user_has_cloud_access(auth.uid()))
  with check (auth.uid() = user_id);

drop policy if exists sites_delete_own on public.sites;
create policy sites_delete_own
  on public.sites for delete to authenticated
  using (auth.uid() = user_id and public.user_has_cloud_access(auth.uid()));

-- ---------------------------------------------------------------------------
-- 5) Storage: allow authenticated devices to sign/download firmware objects
--    (bucket "firmware" must exist and be private)
-- ---------------------------------------------------------------------------
-- Run in SQL Editor if storage policies are not already present:
--
-- insert into storage.buckets (id, name, public)
-- values ('firmware', 'firmware', false)
-- on conflict (id) do nothing;
--
-- drop policy if exists firmware_select_authenticated on storage.objects;
-- create policy firmware_select_authenticated
--   on storage.objects for select to authenticated
--   using (bucket_id = 'firmware');
