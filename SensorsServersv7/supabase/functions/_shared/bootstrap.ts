/** Project connection snapshot stored on provisioning rows / returned by claim-device. */

export type DeviceBootstrapConfig = {
  project_url: string;
  anon_key: string;
  mint_path: string;
  device_api_path: string;
  claim_path: string;
  api_version: number;
};

export function currentBootstrapConfig(): DeviceBootstrapConfig {
  const project_url = (Deno.env.get("SUPABASE_URL") || "").replace(/\/$/, "");
  const anon_key = Deno.env.get("SUPABASE_ANON_KEY") || "";
  return {
    project_url,
    anon_key,
    mint_path: "/functions/v1/mint-device-jwt",
    device_api_path: "/functions/v1/device-api",
    claim_path: "/functions/v1/claim-device",
    api_version: 1,
  };
}
