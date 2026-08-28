/** Resolve/create a site for a user. Returns { id, slug, name }. */

export type SiteInfo = {
  id: string;
  slug: string;
  name: string;
};

export async function ensureSiteForUser(
  // deno-lint-ignore no-explicit-any
  admin: any,
  userId: string,
  siteSlug?: string | null,
  siteName?: string | null,
): Promise<SiteInfo> {
  const slugIn = (siteSlug && siteSlug.trim()) ? siteSlug.trim() : "home";
  const { data: siteId, error } = await admin.rpc("ensure_site", {
    p_user_id: userId,
    p_slug: slugIn,
    p_name: siteName ?? null,
  });
  if (error || !siteId) {
    throw new Error(error?.message || "ensure_site failed");
  }

  const { data: row, error: findErr } = await admin
    .from("sites")
    .select("id, slug, name")
    .eq("id", siteId)
    .single();
  if (findErr || !row) {
    throw new Error(findErr?.message || "site lookup failed");
  }
  return { id: row.id, slug: row.slug, name: row.name };
}

/** Look up site id by slug for a user (no create). */
export async function findSiteIdBySlug(
  // deno-lint-ignore no-explicit-any
  admin: any,
  userId: string,
  siteSlug: string,
): Promise<string | null> {
  const { data, error } = await admin.rpc("normalize_site_slug", {
    p_slug: siteSlug,
  });
  const slug = error ? siteSlug.trim().toLowerCase() : (data as string);
  const { data: row } = await admin
    .from("sites")
    .select("id")
    .eq("user_id", userId)
    .eq("slug", slug)
    .maybeSingle();
  return row?.id ?? null;
}

export type DeleteSiteResult = {
  deletedSlug: string;
  targetSlug: string;
  targetId: string;
  devicesMoved: number;
  createdHome: boolean;
};

/**
 * Delete a site. Devices on it move to the first remaining site (prefer "home"),
 * or to a newly created "home" if this was the user's only site.
 */
export async function deleteSiteForUser(
  // deno-lint-ignore no-explicit-any
  admin: any,
  userId: string,
  siteSlug: string,
): Promise<DeleteSiteResult> {
  const { data: norm, error: normErr } = await admin.rpc("normalize_site_slug", {
    p_slug: siteSlug,
  });
  const slug = normErr ? siteSlug.trim().toLowerCase() : (norm as string);

  const { data: victim, error: findErr } = await admin
    .from("sites")
    .select("id, slug, name")
    .eq("user_id", userId)
    .eq("slug", slug)
    .maybeSingle();
  if (findErr) throw new Error(findErr.message || "site lookup failed");
  if (!victim) throw new Error("site_not_found");

  const { data: allSites, error: listErr } = await admin
    .from("sites")
    .select("id, slug, name")
    .eq("user_id", userId)
    .order("slug");
  if (listErr) throw new Error(listErr.message || "list sites failed");

  const remaining = (allSites ?? []).filter((s: { id: string }) => s.id !== victim.id);
  let createdHome = false;
  let target: SiteInfo;

  if (remaining.length > 0) {
    const home = remaining.find((s: { slug: string }) => s.slug === "home");
    const pick = home ?? remaining[0];
    target = { id: pick.id, slug: pick.slug, name: pick.name };
  } else {
    // Sole site: clear site_id so we can delete, then ensure "home" and reassign.
    const { error: clearErr } = await admin
      .from("devices")
      .update({ site_id: null })
      .eq("user_id", userId)
      .eq("site_id", victim.id);
    if (clearErr) throw new Error(clearErr.message || "clear devices failed");

    const { error: delErr } = await admin
      .from("sites")
      .delete()
      .eq("id", victim.id)
      .eq("user_id", userId);
    if (delErr) throw new Error(delErr.message || "delete site failed");

    target = await ensureSiteForUser(admin, userId, "home", "home");
    createdHome = true;

    const { data: moved, error: moveErr } = await admin
      .from("devices")
      .update({ site_id: target.id })
      .eq("user_id", userId)
      .is("site_id", null)
      .select("id");
    if (moveErr) throw new Error(moveErr.message || "reassign devices failed");

    return {
      deletedSlug: victim.slug,
      targetSlug: target.slug,
      targetId: target.id,
      devicesMoved: (moved ?? []).length,
      createdHome,
    };
  }

  // Multiple sites: move devices to target, then delete victim.
  const { data: moved, error: moveErr } = await admin
    .from("devices")
    .update({ site_id: target.id })
    .eq("user_id", userId)
    .eq("site_id", victim.id)
    .select("id");
  if (moveErr) throw new Error(moveErr.message || "reassign devices failed");

  const { error: delErr } = await admin
    .from("sites")
    .delete()
    .eq("id", victim.id)
    .eq("user_id", userId);
  if (delErr) throw new Error(delErr.message || "delete site failed");

  return {
    deletedSlug: victim.slug,
    targetSlug: target.slug,
    targetId: target.id,
    devicesMoved: (moved ?? []).length,
    createdHome,
  };
}
