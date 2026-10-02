-- Hub-centric uploads: dedupe sensor_readings by time_read; sensors row only moves forward.
-- Run after schema_device_auth_ip.sql (insert_my_reading + 10-sensor cap).

-- Unique identity for a sample: same user + device + sensor + time_read → one row.
-- Null time_read rows are not covered by this unique index (partial); insert_my_reading requires time_read.
create unique index if not exists sensor_readings_user_mac_sns_time_read_uidx
  on public.sensor_readings (user_id, device_mac, sns_type, sns_id, time_read)
  where time_read is not null;

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

  if p_time_read is null then
    raise exception 'time_read_required' using errcode = '22023';
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
  on conflict (user_id, device_mac, sns_type, sns_id, time_read)
    where time_read is not null
  do nothing
  returning id into v_id;

  -- Conflict (duplicate time_read): treat as success; return existing id.
  if v_id is null then
    select id into v_id
    from public.sensor_readings
    where user_id = uid
      and device_mac = v_mac
      and sns_type = p_sns_type
      and sns_id = p_sns_id
      and time_read = p_time_read
    limit 1;
  end if;

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
      updated_at = now()
    where excluded.time_read is not null
      and (
        sensors.time_read is null
        or excluded.time_read > sensors.time_read
      );
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
