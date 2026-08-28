/**
 * DEPRECATED: Device firmware no longer calls device-api.
 * Use PostgREST (/rest/v1) + RPCs from schema_postgrest_device.sql.
 * Kept deployed only so old firmware gets a clear error instead of silent breakage.
 */
import { jsonResponse, optionsResponse } from "../_shared/cors.ts";

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return optionsResponse();
  return jsonResponse(
    {
      api_version: 1,
      ok: false,
      error_code: "deprecated",
      data: {
        message:
          "device-api Edge Function is retired. Flash firmware that uses PostgREST (/rest/v1) and run schema_postgrest_device.sql.",
      },
    },
    410,
  );
});
