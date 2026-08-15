-- One-time device credential pickup (180 days).
-- Plaintext api_key is ONLY readable via service_role (Edge Functions).
-- No grants to anon/authenticated — do not expose via PostgREST clients.

create table if not exists public.device_provisioning (
  id                 uuid primary key default gen_random_uuid(),
  device_mac         text not null,
  user_id            uuid not null references auth.users (id) on delete cascade,
  user_email         text not null,
  api_key_plaintext  text not null,
  claim_code         text not null,
  expires_at         timestamptz not null,
  created_at         timestamptz not null default now(),
  created_by         uuid null references auth.users (id) on delete set null,
  project_url        text null,
  anon_key           text null,
  mint_path          text not null default '/functions/v1/mint-device-jwt',
  device_api_path    text not null default '/functions/v1/device-api',
  claim_path         text not null default '/functions/v1/claim-device',
  api_version        integer not null default 1,
  constraint device_provisioning_mac_unique unique (device_mac),
  constraint device_provisioning_claim_code_len check (char_length(claim_code) = 4)
);

create index if not exists device_provisioning_claim_idx
  on public.device_provisioning (device_mac, claim_code);

create index if not exists device_provisioning_expires_idx
  on public.device_provisioning (expires_at);

comment on table public.device_provisioning is
  'Staging mailbox for one-time device credential claim. Access only via Edge Functions (service_role).';
comment on column public.device_provisioning.claim_code is
  '4-character alphanumeric claim code (A-Z0-9). Compared case-insensitively.';
comment on column public.device_provisioning.expires_at is
  'Default 180 days from provision. Row deleted on successful claim.';

alter table public.device_provisioning enable row level security;

-- Intentionally no policies for authenticated/anon → deny all under RLS.
revoke all on public.device_provisioning from anon, authenticated, public;
grant all on public.device_provisioning to service_role;
