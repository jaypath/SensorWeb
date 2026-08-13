-- SensorWeb cloud schema (Supabase / Postgres)
-- Run in Supabase SQL Editor (or as a migration).
-- Requires Supabase Auth (auth.users).
-- After this file, also run schema_v2.sql (sensors, subscriptions, firmware, api_versions).
--
-- Security model:
--   - Users authenticate to the app via Supabase Auth.
--   - Devices authenticate via Edge Function: device_mac + api_key -> short-lived JWT (sub = user_id).
--   - RLS uses auth.uid(); never trust a client-supplied user_id alone.
--   - Store only api_key_hash (bcrypt/argon2). Plaintext key shown once at enrollment.

-- ---------------------------------------------------------------------------
-- 1) Devices (enrolled hardware + per-device API keys)
-- ---------------------------------------------------------------------------
create table if not exists public.devices (
  id              uuid primary key default gen_random_uuid(),
  user_id         uuid not null references auth.users (id) on delete cascade,
  device_mac      text not null,
  api_key_hash    text not null,
  api_key_prefix  text null,                 -- e.g. first 8 chars for support/lookup UI
  name            text null,
  is_active       boolean not null default true,
  created_at      timestamptz not null default now(),
  last_seen_at    timestamptz null,
  constraint devices_device_mac_unique unique (device_mac),
  constraint devices_user_mac_unique unique (user_id, device_mac)
);

create index if not exists devices_user_id_idx on public.devices (user_id);
create index if not exists devices_api_key_prefix_idx on public.devices (api_key_prefix);

comment on table public.devices is
  'Enrolled ESP devices. api_key_hash verified only by Edge Functions (service role).';
comment on column public.devices.device_mac is
  'Canonical device MAC string (recommend uppercase hex without separators, e.g. AABBCCDDEEFF).';
comment on column public.devices.api_key_hash is
  'Password-hash of device API key. Never return to clients.';

-- ---------------------------------------------------------------------------
-- 2) Sensor readings (time-series; all timestamps UTC / timestamptz)
-- ---------------------------------------------------------------------------
create table if not exists public.sensor_readings (
  id              bigint generated always as identity primary key,
  created_at      timestamptz not null default now(),
  user_id         uuid not null references auth.users (id) on delete cascade,
  device_mac      text not null,
  device_ip       inet null,
  sns_type        smallint not null,
  sns_id          smallint not null,
  sns_name        text not null default '',
  utc_offset      integer not null default 0,  -- seconds east of UTC at time of upload
  time_logged     timestamptz not null,        -- UTC
  time_read       timestamptz null,            -- UTC
  flagged         boolean not null default false,
  expired         boolean not null default false,
  critical        boolean not null default false,
  sns_value       double precision not null,
  sending_int     integer null,
  flags           smallint not null default 0,
  constraint sensor_readings_device_mac_fk
    foreign key (device_mac) references public.devices (device_mac)
    on delete cascade
);

create index if not exists sensor_readings_user_time_idx
  on public.sensor_readings (user_id, time_logged desc);

create index if not exists sensor_readings_device_sns_time_idx
  on public.sensor_readings (device_mac, sns_type, sns_id, time_logged desc);

create index if not exists sensor_readings_time_logged_idx
  on public.sensor_readings (time_logged desc);

comment on column public.sensor_readings.utc_offset is
  'Base timezone offset in seconds (Prefs.TimeZoneOffset). Local wall = UTC + utc_offset (+ DST if applied separately).';
comment on column public.sensor_readings.time_logged is
  'UTC timestamp when the reading was logged/received.';
comment on column public.sensor_readings.time_read is
  'UTC timestamp when the sensor was sampled.';

-- ---------------------------------------------------------------------------
-- 3) Helper: device owned by current user
-- ---------------------------------------------------------------------------
create or replace function public.is_my_active_device(p_mac text)
returns boolean
language sql
stable
security definer
set search_path = public
as $$
  select exists (
    select 1
    from public.devices d
    where d.device_mac = p_mac
      and d.user_id = auth.uid()
      and d.is_active = true
  );
$$;

revoke all on function public.is_my_active_device(text) from public;
grant execute on function public.is_my_active_device(text) to authenticated, service_role;

-- ---------------------------------------------------------------------------
-- 4) Row Level Security
-- ---------------------------------------------------------------------------
alter table public.devices enable row level security;
alter table public.sensor_readings enable row level security;

-- Devices: owners can list/update metadata; inserts for enrollment from user session
-- (API key plaintext issuance should still go through an Edge Function.)
drop policy if exists devices_select_own on public.devices;
create policy devices_select_own
  on public.devices
  for select
  to authenticated
  using (auth.uid() = user_id);

-- Inserts are performed by enroll-device (service role). No insert policy for authenticated.
drop policy if exists devices_insert_own on public.devices;

drop policy if exists devices_update_own on public.devices;
create policy devices_update_own
  on public.devices
  for update
  to authenticated
  using (auth.uid() = user_id)
  with check (auth.uid() = user_id);

drop policy if exists devices_delete_own on public.devices;
create policy devices_delete_own
  on public.devices
  for delete
  to authenticated
  using (auth.uid() = user_id);

-- Hide hash from direct client reads via a view (preferred UI source)
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
  last_seen_at
from public.devices;

grant select on public.devices_safe to authenticated;

-- Sensor readings: owners only; inserts must target an active owned device
drop policy if exists sensor_readings_select_own on public.sensor_readings;
create policy sensor_readings_select_own
  on public.sensor_readings
  for select
  to authenticated
  using (auth.uid() = user_id);

drop policy if exists sensor_readings_insert_own on public.sensor_readings;
create policy sensor_readings_insert_own
  on public.sensor_readings
  for insert
  to authenticated
  with check (
    auth.uid() = user_id
    and public.is_my_active_device(device_mac)
  );

-- Optional: allow owners to delete their history (omit if append-only)
drop policy if exists sensor_readings_delete_own on public.sensor_readings;
create policy sensor_readings_delete_own
  on public.sensor_readings
  for delete
  to authenticated
  using (auth.uid() = user_id);

-- No update policy on readings by default (append-only time series).

-- ---------------------------------------------------------------------------
-- 5) Grants
-- ---------------------------------------------------------------------------
grant usage on schema public to authenticated, service_role;

-- Devices: do not let clients read api_key_hash (use devices_safe for listing).
revoke all on public.devices from authenticated;
grant select (
  id, user_id, device_mac, api_key_prefix, name, is_active, created_at, last_seen_at
) on public.devices to authenticated;
grant update (name, is_active) on public.devices to authenticated;
grant delete on public.devices to authenticated;
-- Inserts of hashed keys should go through enroll-device (service role).
-- No INSERT grant to authenticated on devices.

grant select, insert, delete on public.sensor_readings to authenticated;

-- Identity / sequences
grant usage, select on all sequences in schema public to authenticated;

-- service_role bypasses RLS automatically; used by Edge Functions for key verify + last_seen_at
grant all on public.devices to service_role;
grant all on public.sensor_readings to service_role;
