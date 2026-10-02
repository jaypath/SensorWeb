-- Public-IP auth recovery + 10-sensor-per-device cap
-- Run after schema.sql / schema_v2.sql / schema_postgrest_device.sql
--
-- 1A: mint-device-jwt logs client public IP; valid api_key always mints;
--     bad/missing key can recover JWT only if client IP matches last_auth_public_ip.
-- 2C: at most 10 sensor identities per device_mac; new identities (and their readings) rejected.

-- ---------------------------------------------------------------------------
-- Devices: last successful mint public IP
-- ---------------------------------------------------------------------------
alter table public.devices
  add column if not exists last_auth_public_ip inet null;

alter table public.devices
  add column if not exists last_auth_at timestamptz null;

comment on column public.devices.last_auth_public_ip is
  'Public client IP from last successful mint-device-jwt (Edge x-forwarded-for). Used for IP recovery when api_key fails.';
comment on column public.devices.last_auth_at is
  'Timestamp of last successful mint-device-jwt.';

-- ---------------------------------------------------------------------------
-- Auth audit log (every successful mint)
-- ---------------------------------------------------------------------------
create table if not exists public.device_auth_log (
  id          bigint generated always as identity primary key,
  device_mac  text not null,
  user_id     uuid not null references auth.users (id) on delete cascade,
  public_ip   inet null,
  method      text not null,
  created_at  timestamptz not null default now(),
  constraint device_auth_log_method_check
    check (method in ('api_key', 'ip_recovery'))
);

create index if not exists device_auth_log_mac_time_idx
  on public.device_auth_log (device_mac, created_at desc);

create index if not exists device_auth_log_user_time_idx
  on public.device_auth_log (user_id, created_at desc);

comment on table public.device_auth_log is
  'Successful mint-device-jwt events. Written by Edge (service_role). Not exposed to device JWT.';

alter table public.device_auth_log enable row level security;

-- No policies for authenticated: devices/apps should not read this via PostgREST.
-- service_role bypasses RLS.

revoke all on table public.device_auth_log from public;
grant select, insert on table public.device_auth_log to service_role;

-- ---------------------------------------------------------------------------
-- Sensors: stable registration time for "first 10"
-- ---------------------------------------------------------------------------
alter table public.sensors
  add column if not exists created_at timestamptz;

update public.sensors
set created_at = coalesce(time_logged, updated_at, now())
where created_at is null;

alter table public.sensors
  alter column created_at set default now();

alter table public.sensors
  alter column created_at set not null;

comment on column public.sensors.created_at is
  'First registration time for this (device_mac, sns_type, sns_id). Used for 10-sensor cap.';

-- ---------------------------------------------------------------------------
-- Helper: allow if identity already exists OR fewer than 10 identities for MAC
-- Grandfather: existing rows always allowed (even if count > 10).
-- ---------------------------------------------------------------------------
create or replace function public.device_sensor_slot_allowed(
  p_mac text,
  p_type smallint,
  p_id smallint
)
returns boolean
language plpgsql
stable
security definer
set search_path = public
as $$
declare
  v_mac text;
  v_cnt integer;
begin
  v_mac := upper(regexp_replace(coalesce(p_mac, ''), '[^0-9A-Fa-f]', '', 'g'));
  if char_length(v_mac) <> 12 then
    return false;
  end if;

  if exists (
    select 1
    from public.sensors s
    where s.device_mac = v_mac
      and s.sns_type = p_type
      and s.sns_id = p_id
  ) then
    return true;
  end if;

  select count(*)::integer into v_cnt
  from public.sensors s
  where s.device_mac = v_mac;

  return coalesce(v_cnt, 0) < 10;
end;
$$;

revoke all on function public.device_sensor_slot_allowed(text, smallint, smallint) from public;
grant execute on function public.device_sensor_slot_allowed(text, smallint, smallint)
  to authenticated, service_role;

-- ---------------------------------------------------------------------------
-- BEFORE INSERT on sensors (blocks direct PostgREST insert of 11th identity)
-- ---------------------------------------------------------------------------
create or replace function public.sensors_enforce_max_10()
returns trigger
language plpgsql
security definer
set search_path = public
as $$
begin
  if not public.device_sensor_slot_allowed(new.device_mac, new.sns_type, new.sns_id) then
    raise exception 'sensor_limit'
      using errcode = 'P0001',
            hint = 'Device may have at most 10 sensor identities';
  end if;
  if new.created_at is null then
    new.created_at := now();
  end if;
  return new;
end;
$$;

drop trigger if exists sensors_enforce_max_10_trg on public.sensors;
create trigger sensors_enforce_max_10_trg
  before insert on public.sensors
  for each row
  execute function public.sensors_enforce_max_10();

-- Also block isolated sensor_readings inserts for a new 11th identity
create or replace function public.sensor_readings_enforce_max_10()
returns trigger
language plpgsql
security definer
set search_path = public
as $$
begin
  if not public.device_sensor_slot_allowed(new.device_mac, new.sns_type, new.sns_id) then
    raise exception 'sensor_limit'
      using errcode = 'P0001',
            hint = 'Device may have at most 10 sensor identities';
  end if;
  return new;
end;
$$;

drop trigger if exists sensor_readings_enforce_max_10_trg on public.sensor_readings;
create trigger sensor_readings_enforce_max_10_trg
  before insert on public.sensor_readings
  for each row
  execute function public.sensor_readings_enforce_max_10();

-- ---------------------------------------------------------------------------
-- insert_my_reading: reject new identity (+ reading) when over cap
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

  if not public.device_sensor_slot_allowed(v_mac, p_sns_type, p_sns_id) then
    raise exception 'sensor_limit'
      using errcode = 'P0001',
            hint = 'Device may have at most 10 sensor identities';
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
      time_read, time_logged, sending_int, flags, expired, utc_offset, updated_at, created_at
    ) values (
      v_mac, p_sns_type, p_sns_id, uid, coalesce(p_sns_name, ''), p_sns_value,
      p_time_read, coalesce(p_time_logged, now()), coalesce(p_sending_int, 300),
      coalesce(p_flags, 0), coalesce(p_expired, false), coalesce(p_utc_offset, 0), now(), now()
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
