-- Edge Function rate limits / blocks (service_role only via Edge Functions).

create table if not exists public.edge_rate_limits (
  bucket_key   text primary key,
  window_start timestamptz not null,
  hit_count    integer not null default 0
);

create table if not exists public.blocked_ips (
  ip         text primary key,
  reason     text null,
  hit_count  integer null,
  created_at timestamptz not null default now()
);

create table if not exists public.blocked_macs (
  device_mac text primary key,
  reason     text null,
  source_ip  text null,
  hit_count  integer null,
  created_at timestamptz not null default now()
);

alter table public.edge_rate_limits enable row level security;
alter table public.blocked_ips enable row level security;
alter table public.blocked_macs enable row level security;

revoke all on public.edge_rate_limits from anon, authenticated, public;
revoke all on public.blocked_ips from anon, authenticated, public;
revoke all on public.blocked_macs from anon, authenticated, public;

grant all on public.edge_rate_limits to service_role;
grant all on public.blocked_ips to service_role;
grant all on public.blocked_macs to service_role;

-- Atomically bump a 60s sliding window counter; returns new count in window.
create or replace function public.edge_rate_hit(p_key text, p_window_secs integer default 60)
returns integer
language plpgsql
security definer
set search_path = public
as $$
declare
  v_now timestamptz := now();
  v_count integer;
begin
  insert into public.edge_rate_limits (bucket_key, window_start, hit_count)
  values (p_key, v_now, 1)
  on conflict (bucket_key) do update
  set
    hit_count = case
      when public.edge_rate_limits.window_start >
           v_now - make_interval(secs => p_window_secs)
        then public.edge_rate_limits.hit_count + 1
      else 1
    end,
    window_start = case
      when public.edge_rate_limits.window_start >
           v_now - make_interval(secs => p_window_secs)
        then public.edge_rate_limits.window_start
      else v_now
    end
  returning hit_count into v_count;

  return v_count;
end;
$$;

revoke all on function public.edge_rate_hit(text, integer) from public;
grant execute on function public.edge_rate_hit(text, integer) to service_role;

comment on table public.blocked_ips is
  'IPs blocked after exceeding edge rate limits (e.g. >200/min). Clear manually to unblock.';
comment on table public.blocked_macs is
  'MACs blocked after IP+MAC abuse (e.g. >30/min). Clear manually to unblock.';
