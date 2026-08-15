import { serviceClient } from "./supabase.ts";

/** Soft limits (429). */
export const RATE_IP_MAC_PER_MIN = 5;
export const RATE_IP_PER_MIN = 15;

/** Hard block thresholds. */
export const BLOCK_IP_MAC_PER_MIN = 30;
export const BLOCK_IP_PER_MIN = 200;

export type RateLimitResult =
  | { ok: true }
  | {
    ok: false;
    status: 403 | 429;
    error: string;
    code: "ip_blocked" | "mac_blocked" | "rate_limited";
  };

export function clientIp(req: Request): string {
  const xf = req.headers.get("x-forwarded-for");
  if (xf) {
    const first = xf.split(",")[0]?.trim();
    if (first) return first.slice(0, 64);
  }
  const cf = req.headers.get("cf-connecting-ip")?.trim();
  if (cf) return cf.slice(0, 64);
  const real = req.headers.get("x-real-ip")?.trim();
  if (real) return real.slice(0, 64);
  return "unknown";
}

/**
 * Enforce IP + optional MAC rate limits / blocks.
 * Counts this attempt first, then applies soft 429 and hard block rules.
 */
export async function enforceRateLimits(
  req: Request,
  deviceMac: string | null,
): Promise<RateLimitResult> {
  const ip = clientIp(req);
  const mac = deviceMac && deviceMac.length === 12 ? deviceMac.toUpperCase() : null;
  const admin = serviceClient();

  // --- Existing blocks ---
  const { data: blockedIp } = await admin
    .from("blocked_ips")
    .select("ip")
    .eq("ip", ip)
    .maybeSingle();
  if (blockedIp) {
    return {
      ok: false,
      status: 403,
      error: "IP blocked",
      code: "ip_blocked",
    };
  }

  if (mac) {
    const { data: blockedMac } = await admin
      .from("blocked_macs")
      .select("device_mac")
      .eq("device_mac", mac)
      .maybeSingle();
    if (blockedMac) {
      return {
        ok: false,
        status: 403,
        error: "Device MAC blocked",
        code: "mac_blocked",
      };
    }
  }

  // --- Count this hit ---
  const { data: ipCountRaw, error: ipErr } = await admin.rpc("edge_rate_hit", {
    p_key: `ip:${ip}`,
    p_window_secs: 60,
  });
  if (ipErr) {
    console.error("edge_rate_hit ip:", ipErr);
    // Fail open on infra error? Prefer fail closed for abuse endpoints.
    return {
      ok: false,
      status: 429,
      error: "Rate limit unavailable",
      code: "rate_limited",
    };
  }
  const ipCount = Number(ipCountRaw ?? 0);

  let ipMacCount = 0;
  if (mac) {
    const { data: ipMacRaw, error: ipMacErr } = await admin.rpc("edge_rate_hit", {
      p_key: `ipmac:${ip}:${mac}`,
      p_window_secs: 60,
    });
    if (ipMacErr) {
      console.error("edge_rate_hit ipmac:", ipMacErr);
      return {
        ok: false,
        status: 429,
        error: "Rate limit unavailable",
        code: "rate_limited",
      };
    }
    ipMacCount = Number(ipMacRaw ?? 0);
  }

  // --- Hard blocks ---
  if (ipCount > BLOCK_IP_PER_MIN) {
    await admin.from("blocked_ips").upsert({
      ip,
      reason: `Exceeded ${BLOCK_IP_PER_MIN}/min`,
      hit_count: ipCount,
      created_at: new Date().toISOString(),
    }, { onConflict: "ip" });
    return {
      ok: false,
      status: 403,
      error: "IP blocked",
      code: "ip_blocked",
    };
  }

  if (mac && ipMacCount > BLOCK_IP_MAC_PER_MIN) {
    await admin.from("blocked_macs").upsert({
      device_mac: mac,
      reason: `Exceeded ${BLOCK_IP_MAC_PER_MIN}/min for IP+MAC`,
      source_ip: ip,
      hit_count: ipMacCount,
      created_at: new Date().toISOString(),
    }, { onConflict: "device_mac" });
    return {
      ok: false,
      status: 403,
      error: "Device MAC blocked",
      code: "mac_blocked",
    };
  }

  // --- Soft limits ---
  if (mac && ipMacCount > RATE_IP_MAC_PER_MIN) {
    return {
      ok: false,
      status: 429,
      error: `Rate limit exceeded (max ${RATE_IP_MAC_PER_MIN}/min per IP+MAC)`,
      code: "rate_limited",
    };
  }
  if (ipCount > RATE_IP_PER_MIN) {
    return {
      ok: false,
      status: 429,
      error: `Rate limit exceeded (max ${RATE_IP_PER_MIN}/min per IP)`,
      code: "rate_limited",
    };
  }

  return { ok: true };
}
