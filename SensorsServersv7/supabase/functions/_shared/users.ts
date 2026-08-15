/** Shared Auth admin helpers for enroll/provision Edge Functions. */

export function normalizeEmail(raw: string): string | null {
  const e = raw.trim().toLowerCase();
  if (!e || !e.includes("@") || e.length > 254) return null;
  return e;
}

export function randomPassword(bytes = 24): string {
  const buf = new Uint8Array(bytes);
  crypto.getRandomValues(buf);
  let s = "";
  for (const b of buf) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, "A").replace(/\//g, "B").replace(/=+$/g, "") + "!aA1";
}

export async function findUserIdByEmail(
  // deno-lint-ignore no-explicit-any
  admin: any,
  email: string,
): Promise<{ id: string; email: string } | null> {
  let page = 1;
  for (;;) {
    const { data, error } = await admin.auth.admin.listUsers({ page, perPage: 200 });
    if (error) throw error;
    const users = data?.users ?? [];
    const hit = users.find(
      // deno-lint-ignore no-explicit-any
      (u: any) => (u.email || "").toLowerCase() === email,
    );
    if (hit?.id) return { id: hit.id, email: hit.email || email };
    if (users.length < 200) return null;
    page += 1;
    if (page > 50) return null;
  }
}

/**
 * Resolve target user by email or UUID.
 * - email exists → reuse (do not recreate)
 * - email missing → create Auth user
 * - user_id provided → use if exists
 */
export async function resolveTargetUser(
  // deno-lint-ignore no-explicit-any
  admin: any,
  opts: { email?: string | null; user_id?: string | null },
): Promise<{
  userId: string;
  email: string;
  userCreated: boolean;
  temporaryPassword: string | null;
}> {
  const uid = opts.user_id?.trim() || null;
  if (uid) {
    const { data, error } = await admin.auth.admin.getUserById(uid);
    if (error || !data?.user) {
      throw new Error(`user_id not found: ${uid}`);
    }
    return {
      userId: data.user.id,
      email: (data.user.email || "").toLowerCase(),
      userCreated: false,
      temporaryPassword: null,
    };
  }

  const email = normalizeEmail(opts.email ?? "");
  if (!email) throw new Error("email or user_id required");

  const existing = await findUserIdByEmail(admin, email);
  if (existing) {
    return {
      userId: existing.id,
      email: existing.email,
      userCreated: false,
      temporaryPassword: null,
    };
  }

  const temporaryPassword = randomPassword();
  const { data: created, error: createErr } = await admin.auth.admin.createUser({
    email,
    password: temporaryPassword,
    email_confirm: true,
  });
  if (createErr || !created.user) {
    throw new Error(createErr?.message || "Failed to create user");
  }
  return {
    userId: created.user.id,
    email,
    userCreated: true,
    temporaryPassword,
  };
}

export async function ensureTrialSubscription(
  // deno-lint-ignore no-explicit-any
  admin: any,
  userId: string,
  plan = "trial",
): Promise<void> {
  const { data: hasAccess } = await admin.rpc("user_has_cloud_access", {
    p_uid: userId,
  });
  if (hasAccess) return;
  await admin.from("subscriptions").insert({
    user_id: userId,
    plan,
    status: "trial",
    valid_until: null,
    features: {},
  });
}
