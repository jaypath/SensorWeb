import * as jose from "jsr:@panva/jose@6";
import { requireEnv } from "./supabase.ts";

/** Default device token lifetime (seconds). */
export const DEFAULT_EXPIRES_IN = 3600;

/**
 * Mint an HS256 JWT that PostgREST / RLS accept as an authenticated user.
 * Requires secret JWT_SECRET (legacy JWT secret from Project Settings → API).
 *
 * Claims include device_mac for optional policy use via auth.jwt() ->> 'device_mac'.
 */
export async function mintUserJwt(opts: {
  userId: string;
  deviceMac: string;
  expiresInSec?: number;
}): Promise<{ accessToken: string; expiresIn: number; expiresAt: number }> {
  // Cannot use SUPABASE_* — CLI reserves that prefix. Set secret name JWT_SECRET
  // to your project's legacy JWT Secret (Dashboard → Project Settings → API).
  const jwtSecret = requireEnv("JWT_SECRET");
  const supabaseUrl = requireEnv("SUPABASE_URL").replace(/\/$/, "");
  const expiresIn = opts.expiresInSec ?? DEFAULT_EXPIRES_IN;
  const now = Math.floor(Date.now() / 1000);

  const secret = new TextEncoder().encode(jwtSecret);
  const accessToken = await new jose.SignJWT({
    role: "authenticated",
    aud: "authenticated",
    device_mac: opts.deviceMac,
  })
    .setProtectedHeader({ alg: "HS256", typ: "JWT" })
    .setSubject(opts.userId)
    .setIssuer(`${supabaseUrl}/auth/v1`)
    .setIssuedAt(now)
    .setExpirationTime(now + expiresIn)
    .sign(secret);

  return {
    accessToken,
    expiresIn,
    expiresAt: now + expiresIn,
  };
}
