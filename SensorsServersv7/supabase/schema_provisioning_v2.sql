-- Extend device_provisioning with full device bootstrap fields.
-- Run after schema_provisioning.sql

alter table public.device_provisioning
  add column if not exists project_url      text null,
  add column if not exists anon_key         text null,
  add column if not exists mint_path        text not null default '/functions/v1/mint-device-jwt',
  add column if not exists device_api_path  text not null default '/functions/v1/device-api',
  add column if not exists claim_path       text not null default '/functions/v1/claim-device',
  add column if not exists api_version      integer not null default 1;

comment on column public.device_provisioning.project_url is
  'Supabase project URL snapshot (https://xxxx.supabase.co), no trailing slash.';
comment on column public.device_provisioning.anon_key is
  'Publishable/anon key for apikey header (not a secret; still only via Edge claim).';
comment on column public.device_provisioning.mint_path is
  'Path to mint-device-jwt relative to project_url.';
comment on column public.device_provisioning.device_api_path is
  'Path to device-api relative to project_url.';
comment on column public.device_provisioning.api_version is
  'device-api envelope version the device should send.';
