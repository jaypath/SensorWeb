/** Normalize MAC to uppercase hex with no separators (AABBCCDDEEFF). */
export function normalizeMac(raw: string): string | null {
  if (!raw || typeof raw !== "string") return null;
  const hex = raw.replace(/[^0-9a-fA-F]/g, "").toUpperCase();
  if (hex.length !== 12) return null;
  return hex;
}
