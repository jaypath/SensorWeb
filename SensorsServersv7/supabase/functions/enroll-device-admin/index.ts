import { jsonResponse, optionsResponse } from "../_shared/cors.ts";
import { generateApiKey, hashApiKey } from "../_shared/crypto.ts";
import { normalizeMac } from "../_shared/mac.ts";
import { ensureSiteForUser } from "../_shared/sites.ts";
import { serviceClient, userClientFromAuthHeader } from "../_shared/supabase.ts";

type AdminEnrollBody = {
  email?: string;
  device_mac?: string;
  name?: string | null;
  site?: string | null;
  site_name?: string | null;
  /** If true and device already belongs to the target user, issue a new API key. */
  rotate?: boolean;
  /** Optional plan for newly created users (default trial). */
  plan?: string;
  /** If true (default), ensure a subscription row exists for the target user. */
  ensure_subscription?: boolean;
};

function normalizeEmail(raw: string): string | null {
  const e = raw.trim().toLowerCase();
  if (!e || !e.includes("@") || e.length > 254) return null;
  return e;
}

function randomPassword(bytes = 24): string {
  const buf = new Uint8Array(bytes);
  crypto.getRandomValues(buf);
  let s = "";
  for (const b of buf) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, "A").replace(/\//g, "B").replace(/=+$/g, "") + "!aA1";
}

async function findUserIdByEmail(
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
    if (page > 50) return null; // safety
  }
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return optionsResponse();
  if (req.method !== "POST") {
    return jsonResponse({ error: "Method not allowed" }, 405);
  }

  try {
    const authHeader = req.headers.get("Authorization");
    if (!authHeader?.startsWith("Bearer ")) {
      return jsonResponse({ error: "Missing Authorization Bearer token" }, 401);
    }

    const userSb = userClientFromAuthHeader(authHeader);
    const { data: userData, error: userErr } = await userSb.auth.getUser();
    if (userErr || !userData.user) {
      return jsonResponse({ error: "Invalid or expired user session" }, 401);
    }
    const callerId = userData.user.id;

    const admin = serviceClient();
    const { data: isAdmin, error: adminErr } = await admin.rpc("is_admin", {
      p_uid: callerId,
    });
    if (adminErr) {
      console.error("enroll-device-admin is_admin:", adminErr);
      return jsonResponse({ error: "Admin check failed" }, 500);
    }
    if (!isAdmin) {
      return jsonResponse({ error: "Forbidden: caller is not an admin" }, 403);
    }

    let body: AdminEnrollBody;
    try {
      body = await req.json();
    } catch {
      return jsonResponse({ error: "Invalid JSON body" }, 400);
    }

    const email = normalizeEmail(body.email ?? "");
    const deviceMac = normalizeMac(body.device_mac ?? "");
    if (!email) {
      return jsonResponse({ error: "email required" }, 400);
    }
    if (!deviceMac) {
      return jsonResponse({
        error: "device_mac required (12 hex digits, separators optional)",
      }, 400);
    }

    // --- Find or create Auth user by email ---
    // If email exists: do not recreate (reuse). If missing: create.
    let targetUserId: string;
    let userCreated = false;
    let temporaryPassword: string | null = null;

    const existingUser = await findUserIdByEmail(admin, email);
    if (existingUser) {
      targetUserId = existingUser.id;
    } else {
      temporaryPassword = randomPassword();
      const { data: created, error: createErr } = await admin.auth.admin.createUser({
        email,
        password: temporaryPassword,
        email_confirm: true,
      });
      if (createErr || !created.user) {
        console.error("enroll-device-admin createUser:", createErr);
        return jsonResponse({
          error: "Failed to create user",
          detail: createErr?.message ?? null,
        }, 500);
      }
      targetUserId = created.user.id;
      userCreated = true;
    }

    // Ensure subscription for target (especially new users)
    const ensureSub = body.ensure_subscription !== false;
    if (ensureSub) {
      const { data: hasAccess } = await admin.rpc("user_has_cloud_access", {
        p_uid: targetUserId,
      });
      if (!hasAccess) {
        const plan = (body.plan?.trim() || (userCreated ? "trial" : "trial"));
        await admin.from("subscriptions").insert({
          user_id: targetUserId,
          plan,
          status: userCreated ? "trial" : "trial",
          valid_until: null,
          features: {},
        });
      }
    }

    const site = await ensureSiteForUser(admin, targetUserId, body.site, body.site_name);

    // --- Enroll / rotate device under target user ---
    const { data: existingDev, error: findErr } = await admin
      .from("devices")
      .select("id, user_id, is_active")
      .eq("device_mac", deviceMac)
      .maybeSingle();

    if (findErr) {
      console.error("enroll-device-admin find device:", findErr);
      return jsonResponse({ error: "Failed to look up device" }, 500);
    }

    if (existingDev && existingDev.user_id !== targetUserId) {
      return jsonResponse({
        error: "Device MAC is registered to another user",
        device_id: existingDev.id,
      }, 403);
    }

    if (existingDev && !body.rotate) {
      return jsonResponse({
        error: "Device already enrolled for this user. Pass rotate:true to issue a new API key.",
        device_id: existingDev.id,
        user_id: targetUserId,
        email,
        user_created: false,
      }, 409);
    }

    const { apiKey, prefix } = generateApiKey();
    const apiKeyHash = hashApiKey(apiKey);
    const name = body.name?.trim() || null;

    if (existingDev && body.rotate) {
      const { data: updated, error: updErr } = await admin
        .from("devices")
        .update({
          api_key_hash: apiKeyHash,
          api_key_prefix: prefix,
          name: name ?? undefined,
          is_active: true,
          site_id: site.id,
        })
        .eq("id", existingDev.id)
        .eq("user_id", targetUserId)
        .select("id, device_mac, api_key_prefix, name, is_active, created_at, site_id")
        .single();

      if (updErr || !updated) {
        console.error("enroll-device-admin rotate:", updErr);
        return jsonResponse({ error: "Failed to rotate device key" }, 500);
      }

      return jsonResponse({
        ...updated,
        api_key: apiKey,
        rotated: true,
        user_id: targetUserId,
        email,
        user_created: userCreated,
        temporary_password: temporaryPassword,
        site_slug: site.slug,
        site_name: site.name,
        warning:
          "Store api_key on the device now; it cannot be retrieved again." +
          (temporaryPassword
            ? " temporary_password is shown once for the new Auth user."
            : ""),
      });
    }

    const { data: createdDev, error: insErr } = await admin
      .from("devices")
      .insert({
        user_id: targetUserId,
        device_mac: deviceMac,
        api_key_hash: apiKeyHash,
        api_key_prefix: prefix,
        name,
        is_active: true,
        site_id: site.id,
      })
      .select("id, device_mac, api_key_prefix, name, is_active, created_at, site_id")
      .single();

    if (insErr || !createdDev) {
      console.error("enroll-device-admin insert:", insErr);
      return jsonResponse({ error: "Failed to enroll device" }, 500);
    }

    return jsonResponse({
      ...createdDev,
      api_key: apiKey,
      rotated: false,
      user_id: targetUserId,
      email,
      user_created: userCreated,
      temporary_password: temporaryPassword,
      site_slug: site.slug,
      site_name: site.name,
      warning:
        "Store api_key on the device now; it cannot be retrieved again." +
        (temporaryPassword
          ? " temporary_password is shown once for the new Auth user."
          : ""),
    }, 201);
  } catch (e) {
    console.error("enroll-device-admin:", e);
    return jsonResponse({ error: "Internal error" }, 500);
  }
});
