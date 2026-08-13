import bcrypt from "npm:bcryptjs@2.4.3";

const API_KEY_PREFIX_LEN = 8;
const BCRYPT_ROUNDS = 10;

function bytesToBase64Url(bytes: Uint8Array): string {
  let bin = "";
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

/** Generate a device API key. Plaintext is shown once to the client/device. */
export function generateApiKey(): { apiKey: string; prefix: string } {
  const bytes = new Uint8Array(32);
  crypto.getRandomValues(bytes);
  const apiKey = `sw_${bytesToBase64Url(bytes)}`;
  return { apiKey, prefix: apiKey.slice(0, API_KEY_PREFIX_LEN) };
}

export function hashApiKey(apiKey: string): string {
  return bcrypt.hashSync(apiKey, BCRYPT_ROUNDS);
}

export function verifyApiKey(apiKey: string, hash: string): boolean {
  return bcrypt.compareSync(apiKey, hash);
}
