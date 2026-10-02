-- ArborysNet naming: site label (was slug) max 24, site description (was name) max 64.
-- Run after schema_sites.sql.

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
  if char_length(s) > 24 then
    s := left(s, 24);
  end if;
  return s;
end;
$$;

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
  if char_length(v_name) > 64 then
    v_name := left(v_name, 64);
  end if;

  select id into v_id
  from public.sites
  where user_id = p_user_id and slug = v_slug;

  if v_id is null then
    insert into public.sites (user_id, slug, name)
    values (p_user_id, v_slug, v_name)
    on conflict (user_id, slug) do update set name = excluded.name
    returning id into v_id;
  else
    -- Keep description in sync when provided
    if p_name is not null then
      update public.sites set name = v_name where id = v_id;
    end if;
  end if;

  return v_id;
end;
$$;

-- Tighten check: label max 24 (was 32)
alter table public.sites drop constraint if exists sites_slug_format;
alter table public.sites
  add constraint sites_slug_format check (slug ~ '^[a-z0-9][a-z0-9_-]{0,23}$');

comment on column public.sites.slug is 'ArborysNet site label (max 24)';
comment on column public.sites.name is 'ArborysNet site description (max 64)';
