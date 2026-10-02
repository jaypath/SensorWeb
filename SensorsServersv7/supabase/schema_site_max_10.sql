-- Cap each user to 10 ArborysNet locations (sites).
-- Run after schema_site_label_limits.sql.
-- Existing sites above 10 are kept; new creates are blocked.

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
  v_count integer;
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
    select count(*)::integer into v_count
    from public.sites
    where user_id = p_user_id;

    if v_count >= 10 then
      raise exception 'site_limit: max 10 locations per account'
        using errcode = 'P0001';
    end if;

    insert into public.sites (user_id, slug, name)
    values (p_user_id, v_slug, v_name)
    on conflict (user_id, slug) do update set name = excluded.name
    returning id into v_id;
  else
    if p_name is not null then
      update public.sites set name = v_name where id = v_id;
    end if;
  end if;

  return v_id;
end;
$$;

comment on function public.ensure_site(uuid, text, text) is
  'Create/update a site for a user. Max 10 sites per user; existing slug updates description.';
