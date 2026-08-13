import * as jose from "jsr:@panva/jose@6";
import { requireEnv } from "./supabase.ts";

export type VerifiedDeviceJwt = {
  userId: string;
  deviceMac: string | null;
  role: string;
};

/** Verify HS256 Supabase-compatible access token (legacy JWT_SECRET). */
export async function verifyUserJwt(token: string): Promise<VerifiedDeviceJwt> {
  const jwtSecret = requireEnv("JWT_SECRET");
  const supabaseUrl = requireEnv("SUPABASE_URL").replace(/\/$/, "");
  const secret = new TextEncoder().encode(jwtSecret);

  const { payload } = await jose.jwtVerify(token, secret, {
    issuer: `${supabaseUrl}/auth/v1`,
    audience: "authenticated",
  });

  const userId = typeof payload.sub === "string" ? payload.sub : "";
  if (!userId) throw new Error("JWT missing sub");

  const deviceMac =
    typeof payload.device_mac === "string" ? payload.device_mac.toUpperCase() : null;

  return {
    userId,
    deviceMac,
    role: typeof payload.role === "string" ? payload.role : "authenticated",
  };
}

export function bearerToken(req: Request): string | null {
  const h = req.headers.get("Authorization");
  if (!h?.startsWith("Bearer ")) return null;
  const t = h.slice(7).trim();
  return t.length > 0 ? t : null;
}
