-- SensorWeb cloud schema v2
-- Run AFTER schema.sql in Supabase SQL Editor.
--
-- Adds: device registry fields, sensors current-state table, subscriptions,
-- api_versions gate, firmware_releases catalog. Create private Storage bucket
-- named "firmware" in Dashboard → Storage (path e.g. 101/32/1.2.3.bin).

-- ---------------------------------------------------------------------------
-- 1) Extend devices (global device registry fields)
-- ---------------------------------------------------------------------------
alter table public.devices
  add column if not exists device_ip      inet null,
  add column if not exists dev_type       smallint not null default 0,
  add column if not exists feature_mask   bigint not null default 0,
  add column if not exists dev_name       text null,
  add column if not exists sending_int    integer null,
  add column if not exists firmware_major smallint not null default 0,
  add column if not exists firmware_minor smallint not null default 0,
  add column if not exists firmware_patch smallint not null default 0,
  add column if not exists expired        boolean not null default false,
  add column if not exists flags          smallint not null default 0,
  add column if not exists data_received  timestamptz null,
  add column if not exists data_sent      timestamptz null;

comment on column public.devices.dev_type is
  'Device type (e.g. 101 weather hub). Paired with feature_mask for firmware matching.';
comment on column public.devices.feature_mask is
  'uint32 feature bitmask stored as bigint. Firmware releases require (device.mask & release.mask) = release.mask.';

-- Refresh devices_safe view with new columns (still hides api_key_hash)
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
  data_sent
from public.devices;

grant select on public.devices_safe to authenticated;

-- Allow authenticated owners to update core device metadata columns
grant update (
  name, is_active, device_ip, dev_type, feature_mask, dev_name, sending_int,
  firmware_major, firmware_minor, firmware_patch, expired, flags,
  data_received, data_sent, last_seen_at
) on public.devices to authenticated;

grant select (
  id, user_id, device_mac, api_key_prefix, name, is_active, created_at, last_seen_at,
  device_ip, dev_type, feature_mask, dev_name, sending_int,
  firmware_major, firmware_minor, firmware_patch, expired, flags,
  data_received, data_sent
) on public.devices to authenticated;

-- ---------------------------------------------------------------------------
-- 2) sensors — current device/sensor combo state
-- ---------------------------------------------------------------------------
create table if not exists public.sensors (
  device_mac    text not null references public.devices (device_mac) on delete cascade,
  sns_type      smallint not null,
  sns_id        smallint not null,
  user_id       uuid not null references auth.users (id) on delete cascade,
  sns_name      text not null default '',
  sns_value     double precision not null default 0,
  time_read     timestamptz null,
  time_logged   timestamptz null,
  sending_int   integer not null default 300,
  flags         smallint not null default 0,
  expired       boolean not null default false,
  limit_high    double precision null,
  limit_low     double precision null,
  utc_offset    integer not null default 0,
  updated_at    timestamptz not null default now(),
  primary key (device_mac, sns_type, sns_id)
);

create index if not exists sensors_user_expired_time_idx
  on public.sensors (user_id, expired, time_read desc);

create index if not exists sensors_user_mac_idx
  on public.sensors (user_id, device_mac);

comment on table public.sensors is
  'Current state per device-sensor combo. History lives in sensor_readings.';

alter table public.sensors enable row level security;

drop policy if exists sensors_select_own on public.sensors;
create policy sensors_select_own
  on public.sensors for select to authenticated
  using (auth.uid() = user_id);

drop policy if exists sensors_insert_own on public.sensors;
create policy sensors_insert_own
  on public.sensors for insert to authenticated
  with check (
    auth.uid() = user_id
    and public.is_my_active_device(device_mac)
  );

drop policy if exists sensors_update_own on public.sensors;
create policy sensors_update_own
  on public.sensors for update to authenticated
  using (auth.uid() = user_id)
  with check (
    auth.uid() = user_id
    and public.is_my_active_device(device_mac)
  );

drop policy if exists sensors_delete_own on public.sensors;
create policy sensors_delete_own
  on public.sensors for delete to authenticated
  using (auth.uid() = user_id);

grant select, insert, update, delete on public.sensors to authenticated;
grant all on public.sensors to service_role;

-- ---------------------------------------------------------------------------
-- 3) subscriptions — access / future paywall skeleton
-- ---------------------------------------------------------------------------
create table if not exists public.subscriptions (
  id           uuid primary key default gen_random_uuid(),
  user_id      uuid not null references auth.users (id) on delete cascade,
  plan         text not null default 'trial',
  status       text not null default 'active'
               check (status in ('active', 'expired', 'trial', 'suspended')),
  valid_until  timestamptz null,
  features     jsonb not null default '{}'::jsonb,
  created_at   timestamptz not null default now(),
  updated_at   timestamptz not null default now()
);

create index if not exists subscriptions_user_id_idx on public.subscriptions (user_id);

alter table public.subscriptions enable row level security;

drop policy if exists subscriptions_select_own on public.subscriptions;
create policy subscriptions_select_own
  on public.subscriptions for select to authenticated
  using (auth.uid() = user_id);

grant select on public.subscriptions to authenticated;
grant all on public.subscriptions to service_role;

-- True if user has at least one non-expired active/trial subscription.
-- If no subscription row exists, access is DENIED (enroll/admin must grant trial).
create or replace function public.user_has_cloud_access(p_uid uuid)
returns boolean
language sql
stable
security definer
set search_path = public
as $$
  select exists (
    select 1
    from public.subscriptions s
    where s.user_id = p_uid
      and s.status in ('active', 'trial')
      and (s.valid_until is null or s.valid_until > now())
  );
$$;

revoke all on function public.user_has_cloud_access(uuid) from public;
grant execute on function public.user_has_cloud_access(uuid) to authenticated, service_role;

-- ---------------------------------------------------------------------------
-- 4) api_versions — min version gate for device-api
-- ---------------------------------------------------------------------------
create table if not exists public.api_versions (
  api_version   integer primary key,
  is_supported  boolean not null default true,
  notes         text null,
  created_at    timestamptz not null default now()
);

-- Singleton-style config: min accepted api_version for device-api
create table if not exists public.api_config (
  id               boolean primary key default true check (id),
  min_api_version  integer not null default 1,
  updated_at       timestamptz not null default now()
);

insert into public.api_versions (api_version, is_supported, notes)
values (1, true, 'Initial device-api envelope')
on conflict (api_version) do nothing;

insert into public.api_config (id, min_api_version)
values (true, 1)
on conflict (id) do nothing;

alter table public.api_versions enable row level security;
alter table public.api_config enable row level security;

drop policy if exists api_versions_select_auth on public.api_versions;
create policy api_versions_select_auth
  on public.api_versions for select to authenticated
  using (true);

drop policy if exists api_config_select_auth on public.api_config;
create policy api_config_select_auth
  on public.api_config for select to authenticated
  using (true);

grant select on public.api_versions to authenticated;
grant select on public.api_config to authenticated;
grant all on public.api_versions to service_role;
grant all on public.api_config to service_role;

create or replace function public.get_min_api_version()
returns integer
language sql
stable
security definer
set search_path = public
as $$
  select coalesce((select min_api_version from public.api_config where id = true), 1);
$$;

revoke all on function public.get_min_api_version() from public;
grant execute on function public.get_min_api_version() to authenticated, service_role;

-- ---------------------------------------------------------------------------
-- 5) firmware_releases catalog
-- ---------------------------------------------------------------------------
create table if not exists public.firmware_releases (
  id              uuid primary key default gen_random_uuid(),
  dev_type        smallint not null,
  feature_mask    bigint not null default 0,
  version_major   smallint not null,
  version_minor   smallint not null,
  version_patch   smallint not null,
  storage_path    text not null,
  sha256          text null,
  size_bytes      integer null,
  is_active       boolean not null default true,
  notes           text null,
  created_at      timestamptz not null default now(),
  constraint firmware_releases_version_unique
    unique (dev_type, feature_mask, version_major, version_minor, version_patch)
);

create index if not exists firmware_releases_type_mask_idx
  on public.firmware_releases (dev_type, feature_mask, is_active);

comment on table public.firmware_releases is
  'Firmware catalog. Files live in private Storage bucket "firmware" at storage_path.';
comment on column public.firmware_releases.storage_path is
  'Path inside bucket firmware, e.g. 101/32/9.4.22.bin';

alter table public.firmware_releases enable row level security;

drop policy if exists firmware_releases_select_auth on public.firmware_releases;
create policy firmware_releases_select_auth
  on public.firmware_releases for select to authenticated
  using (is_active = true);

grant select on public.firmware_releases to authenticated;
grant all on public.firmware_releases to service_role;

-- ---------------------------------------------------------------------------
-- 6) Freshness helper (matches firmware sensorExpiryGraceSec: sending_int + sending_int/4)
-- ---------------------------------------------------------------------------
create or replace function public.sensor_is_fresh(
  p_time_read timestamptz,
  p_sending_int integer,
  p_expired boolean
)
returns boolean
language sql
immutable
as $$
  select
    coalesce(p_expired, false) = false
    and p_time_read is not null
    and p_sending_int is not null
    and p_sending_int > 0
    and p_time_read + make_interval(secs => (p_sending_int + p_sending_int / 4)) > now();
$$;

revoke all on function public.sensor_is_fresh(timestamptz, integer, boolean) from public;
grant execute on function public.sensor_is_fresh(timestamptz, integer, boolean)
  to authenticated, service_role;
