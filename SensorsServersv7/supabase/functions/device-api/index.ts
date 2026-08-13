import { jsonResponse, optionsResponse } from "../_shared/cors.ts";
import { bearerToken, verifyUserJwt } from "../_shared/auth.ts";
import { normalizeMac } from "../_shared/mac.ts";
import { serviceClient } from "../_shared/supabase.ts";

type DeviceApiBody = {
  api_version?: number;
  action?: string;
  device_mac?: string;
  params?: Record<string, unknown>;
};

function envelope(
  apiVersion: number,
  ok: boolean,
  data: unknown,
  errorCode: string | null = null,
  status = 200,
): Response {
  return jsonResponse(
    {
      api_version: apiVersion,
      ok,
      error_code: errorCode,
      data,
    },
    status,
  );
}

function asString(v: unknown): string | null {
  return typeof v === "string" ? v : null;
}

function asNumber(v: unknown): number | null {
  return typeof v === "number" && Number.isFinite(v) ? v : null;
}

function asBool(v: unknown): boolean | null {
  return typeof v === "boolean" ? v : null;
}

function unixToIso(v: unknown): string | null {
  const n = asNumber(v);
  if (n == null || n <= 0) return null;
  return new Date(n * 1000).toISOString();
}

function isoToUnix(iso: string | null | undefined): number {
  if (!iso) return 0;
  const ms = Date.parse(iso);
  return Number.isFinite(ms) ? Math.floor(ms / 1000) : 0;
}

function compareFw(
  a: { major: number; minor: number; patch: number },
  b: { major: number; minor: number; patch: number },
): number {
  if (a.major !== b.major) return a.major - b.major;
  if (a.minor !== b.minor) return a.minor - b.minor;
  return a.patch - b.patch;
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return optionsResponse();
  if (req.method !== "POST") {
    return envelope(1, false, null, "method_not_allowed", 405);
  }

  let body: DeviceApiBody;
  try {
    body = await req.json();
  } catch {
    return envelope(1, false, null, "invalid_json", 400);
  }

  const apiVersion = asNumber(body.api_version) ?? 0;
  const action = asString(body.action) ?? "";
  const params = body.params && typeof body.params === "object" ? body.params : {};

  try {
    const admin = serviceClient();

    // --- Gate 1: API version ---
    const { data: cfg } = await admin
      .from("api_config")
      .select("min_api_version")
      .eq("id", true)
      .maybeSingle();
    const minApi = cfg?.min_api_version ?? 1;
    if (apiVersion < minApi) {
      return envelope(apiVersion || minApi, false, {
        min_api_version: minApi,
        message: "Device API version too old; upgrade firmware",
      }, "upgrade_required", 410);
    }

    // --- Gate 2: JWT ---
    const token = bearerToken(req);
    if (!token) {
      return envelope(apiVersion, false, null, "missing_auth", 401);
    }

    let claims;
    try {
      claims = await verifyUserJwt(token);
    } catch (e) {
      console.error("device-api jwt:", e);
      return envelope(apiVersion, false, null, "invalid_auth", 401);
    }

    const reqMac = normalizeMac(body.device_mac ?? claims.deviceMac ?? "");
    if (!reqMac) {
      return envelope(apiVersion, false, null, "device_mac_required", 400);
    }
    if (claims.deviceMac && claims.deviceMac !== reqMac) {
      return envelope(apiVersion, false, null, "mac_mismatch", 403);
    }

    // --- Gate 3: registered + active device ---
    const { data: device, error: devErr } = await admin
      .from("devices")
      .select("*")
      .eq("device_mac", reqMac)
      .maybeSingle();

    if (devErr) {
      console.error("device-api device lookup:", devErr);
      return envelope(apiVersion, false, null, "device_lookup_failed", 500);
    }
    if (!device || !device.is_active) {
      return envelope(apiVersion, false, null, "device_not_registered", 403);
    }
    if (device.user_id !== claims.userId) {
      return envelope(apiVersion, false, null, "device_user_mismatch", 403);
    }

    // --- Gate 4: subscription ---
    const { data: hasAccess, error: subErr } = await admin.rpc(
      "user_has_cloud_access",
      { p_uid: claims.userId },
    );
    if (subErr) {
      console.error("device-api subscription:", subErr);
      return envelope(apiVersion, false, null, "subscription_check_failed", 500);
    }
    if (!hasAccess) {
      return envelope(apiVersion, false, null, "subscription_inactive", 403);
    }

    // Touch last_seen
    await admin
      .from("devices")
      .update({ last_seen_at: new Date().toISOString() })
      .eq("id", device.id);

    // --- Actions ---
    switch (action) {
      case "list_active_sensors": {
        const scopeMac = normalizeMac(asString(params.device_mac) ?? "") || null;
        let q = admin
          .from("sensors")
          .select("*")
          .eq("user_id", claims.userId)
          .eq("expired", false);
        if (scopeMac) q = q.eq("device_mac", scopeMac);

        const { data: rows, error } = await q.limit(
          Math.min(asNumber(params.limit) ?? 200, 500),
        );
        if (error) {
          console.error("list_active_sensors:", error);
          return envelope(apiVersion, false, null, "query_failed", 500);
        }

        const nowMs = Date.now();
        const active = (rows ?? []).filter((r) => {
          if (!r.time_read || !r.sending_int || r.sending_int <= 0) return false;
          const readMs = Date.parse(r.time_read);
          if (!Number.isFinite(readMs)) return false;
          const graceSec = r.sending_int + Math.floor(r.sending_int / 4);
          return readMs + graceSec * 1000 > nowMs;
        }).map(mapSensorRow);

        return envelope(apiVersion, true, { sensors: active });
      }

      case "query": {
        const table = asString(params.table) ?? "sensors";
        const limit = Math.min(asNumber(params.limit) ?? 100, 500);
        const expired = asBool(params.expired);
        const snsType = asNumber(params.sns_type);
        const filterMac = normalizeMac(asString(params.device_mac) ?? "") || null;
        const deviceIp = asString(params.device_ip);
        const timeStart = unixToIso(params.time_start);
        const timeEnd = unixToIso(params.time_end);

        if (table === "devices") {
          let q = admin.from("devices").select(
            "id,user_id,device_mac,name,dev_name,device_ip,dev_type,feature_mask,sending_int,firmware_major,firmware_minor,firmware_patch,expired,flags,is_active,data_received,data_sent,last_seen_at,created_at",
          ).eq("user_id", claims.userId);
          if (filterMac) q = q.eq("device_mac", filterMac);
          if (deviceIp) q = q.eq("device_ip", deviceIp);
          if (expired != null) q = q.eq("expired", expired);
          const { data, error } = await q.limit(limit);
          if (error) {
            console.error("query devices:", error);
            return envelope(apiVersion, false, null, "query_failed", 500);
          }
          return envelope(apiVersion, true, {
            table,
            devices: (data ?? []).map(mapDeviceRow),
          });
        }

        if (table === "sensor_readings") {
          let q = admin
            .from("sensor_readings")
            .select("*")
            .eq("user_id", claims.userId)
            .order("time_logged", { ascending: false });
          if (filterMac) q = q.eq("device_mac", filterMac);
          if (snsType != null) q = q.eq("sns_type", snsType);
          if (expired != null) q = q.eq("expired", expired);
          if (timeStart) q = q.gte("time_logged", timeStart);
          if (timeEnd) q = q.lte("time_logged", timeEnd);
          const { data, error } = await q.limit(limit);
          if (error) {
            console.error("query readings:", error);
            return envelope(apiVersion, false, null, "query_failed", 500);
          }
          return envelope(apiVersion, true, {
            table,
            readings: (data ?? []).map(mapReadingRow),
          });
        }

        // default: sensors
        let q = admin.from("sensors").select("*").eq("user_id", claims.userId);
        if (filterMac) q = q.eq("device_mac", filterMac);
        if (snsType != null) q = q.eq("sns_type", snsType);
        if (expired != null) q = q.eq("expired", expired);
        if (timeStart) q = q.gte("time_read", timeStart);
        if (timeEnd) q = q.lte("time_read", timeEnd);
        const { data, error } = await q.limit(limit);
        if (error) {
          console.error("query sensors:", error);
          return envelope(apiVersion, false, null, "query_failed", 500);
        }
        return envelope(apiVersion, true, {
          table: "sensors",
          sensors: (data ?? []).map(mapSensorRow),
        });
      }

      case "upsert_device": {
        const patch: Record<string, unknown> = {};
        const ip = asString(params.device_ip);
        if (ip != null) patch.device_ip = ip;
        if (asString(params.dev_name) != null) {
          patch.dev_name = asString(params.dev_name);
          patch.name = asString(params.dev_name);
        }
        if (asString(params.name) != null) {
          patch.name = asString(params.name);
          if (patch.dev_name == null) patch.dev_name = asString(params.name);
        }
        if (asNumber(params.dev_type) != null) patch.dev_type = asNumber(params.dev_type);
        if (asNumber(params.feature_mask) != null) {
          patch.feature_mask = asNumber(params.feature_mask);
        }
        if (asNumber(params.sending_int) != null) {
          patch.sending_int = asNumber(params.sending_int);
        }
        if (asNumber(params.firmware_major) != null) {
          patch.firmware_major = asNumber(params.firmware_major);
        }
        if (asNumber(params.firmware_minor) != null) {
          patch.firmware_minor = asNumber(params.firmware_minor);
        }
        if (asNumber(params.firmware_patch) != null) {
          patch.firmware_patch = asNumber(params.firmware_patch);
        }
        if (asBool(params.expired) != null) patch.expired = asBool(params.expired);
        if (asNumber(params.flags) != null) patch.flags = asNumber(params.flags);
        const dr = unixToIso(params.data_received);
        const ds = unixToIso(params.data_sent);
        if (dr) patch.data_received = dr;
        if (ds) patch.data_sent = ds;

        if (Object.keys(patch).length === 0) {
          return envelope(apiVersion, false, null, "no_fields", 400);
        }

        const { data, error } = await admin
          .from("devices")
          .update(patch)
          .eq("device_mac", reqMac)
          .eq("user_id", claims.userId)
          .select(
            "id,user_id,device_mac,name,dev_name,device_ip,dev_type,feature_mask,sending_int,firmware_major,firmware_minor,firmware_patch,expired,flags,is_active,data_received,data_sent,last_seen_at",
          )
          .single();

        if (error) {
          console.error("upsert_device:", error);
          return envelope(apiVersion, false, null, "upsert_failed", 500);
        }
        return envelope(apiVersion, true, { device: mapDeviceRow(data) });
      }

      case "upsert_sensor": {
        const snsType = asNumber(params.sns_type);
        const snsId = asNumber(params.sns_id);
        if (snsType == null || snsId == null) {
          return envelope(apiVersion, false, null, "sns_type_id_required", 400);
        }
        const sensorMac = normalizeMac(asString(params.device_mac) ?? reqMac) ?? reqMac;

        // Sensor must belong to an owned active device
        const { data: ownerDev } = await admin
          .from("devices")
          .select("device_mac,user_id,is_active")
          .eq("device_mac", sensorMac)
          .eq("user_id", claims.userId)
          .maybeSingle();
        if (!ownerDev?.is_active) {
          return envelope(apiVersion, false, null, "sensor_device_not_owned", 403);
        }

        const row = {
          device_mac: sensorMac,
          sns_type: snsType,
          sns_id: snsId,
          user_id: claims.userId,
          sns_name: asString(params.sns_name) ?? "",
          sns_value: asNumber(params.sns_value) ?? 0,
          time_read: unixToIso(params.time_read),
          time_logged: unixToIso(params.time_logged),
          sending_int: asNumber(params.sending_int) ?? 300,
          flags: asNumber(params.flags) ?? 0,
          expired: asBool(params.expired) ?? false,
          limit_high: asNumber(params.limit_high),
          limit_low: asNumber(params.limit_low),
          utc_offset: asNumber(params.utc_offset) ?? 0,
          updated_at: new Date().toISOString(),
        };

        const { data, error } = await admin
          .from("sensors")
          .upsert(row, { onConflict: "device_mac,sns_type,sns_id" })
          .select("*")
          .single();

        if (error) {
          console.error("upsert_sensor:", error);
          return envelope(apiVersion, false, null, "upsert_failed", 500);
        }
        return envelope(apiVersion, true, { sensor: mapSensorRow(data) });
      }

      case "insert_reading": {
        const snsType = asNumber(params.sns_type);
        const snsId = asNumber(params.sns_id);
        const snsValue = asNumber(params.sns_value);
        if (snsType == null || snsId == null || snsValue == null) {
          return envelope(apiVersion, false, null, "reading_fields_required", 400);
        }
        const readingMac = normalizeMac(asString(params.device_mac) ?? reqMac) ?? reqMac;
        const timeLogged = unixToIso(params.time_logged) ?? new Date().toISOString();
        const timeRead = unixToIso(params.time_read);

        const reading = {
          user_id: claims.userId,
          device_mac: readingMac,
          device_ip: asString(params.device_ip),
          sns_type: snsType,
          sns_id: snsId,
          sns_name: asString(params.sns_name) ?? "",
          utc_offset: asNumber(params.utc_offset) ?? 0,
          time_logged: timeLogged,
          time_read: timeRead,
          flagged: asBool(params.flagged) ?? false,
          expired: asBool(params.expired) ?? false,
          critical: asBool(params.critical) ?? false,
          sns_value: snsValue,
          sending_int: asNumber(params.sending_int),
          flags: asNumber(params.flags) ?? 0,
        };

        const { data, error } = await admin
          .from("sensor_readings")
          .insert(reading)
          .select("*")
          .single();
        if (error) {
          console.error("insert_reading:", error);
          return envelope(apiVersion, false, null, "insert_failed", 500);
        }

        const refreshCurrent = asBool(params.refresh_sensor) ?? true;
        if (refreshCurrent) {
          await admin.from("sensors").upsert({
            device_mac: readingMac,
            sns_type: snsType,
            sns_id: snsId,
            user_id: claims.userId,
            sns_name: reading.sns_name,
            sns_value: snsValue,
            time_read: timeRead,
            time_logged: timeLogged,
            sending_int: reading.sending_int ?? 300,
            flags: reading.flags,
            expired: reading.expired,
            utc_offset: reading.utc_offset,
            updated_at: new Date().toISOString(),
          }, { onConflict: "device_mac,sns_type,sns_id" });
        }

        return envelope(apiVersion, true, { reading: mapReadingRow(data) });
      }

      case "firmware_check": {
        const devType = asNumber(params.dev_type) ?? device.dev_type ?? 0;
        const featureMask = asNumber(params.feature_mask) ??
          Number(device.feature_mask ?? 0);
        const cur = {
          major: asNumber(params.firmware_major) ?? device.firmware_major ?? 0,
          minor: asNumber(params.firmware_minor) ?? device.firmware_minor ?? 0,
          patch: asNumber(params.firmware_patch) ?? device.firmware_patch ?? 0,
        };

        const { data: releases, error } = await admin
          .from("firmware_releases")
          .select("*")
          .eq("dev_type", devType)
          .eq("is_active", true);
        if (error) {
          console.error("firmware_check:", error);
          return envelope(apiVersion, false, null, "firmware_query_failed", 500);
        }

        const mask = BigInt(featureMask);
        let best: typeof releases extends (infer T)[] | null ? T : never = null as never;
        let bestVer = cur;

        for (const r of releases ?? []) {
          const reqMask = BigInt(r.feature_mask ?? 0);
          if ((mask & reqMask) !== reqMask) continue;
          const ver = {
            major: r.version_major,
            minor: r.version_minor,
            patch: r.version_patch,
          };
          if (compareFw(ver, bestVer) > 0) {
            best = r;
            bestVer = ver;
          }
        }

        if (!best) {
          return envelope(apiVersion, true, {
            available: false,
            current: cur,
          });
        }

        return envelope(apiVersion, true, {
          available: true,
          current: cur,
          release: {
            id: best.id,
            dev_type: best.dev_type,
            feature_mask: Number(best.feature_mask),
            version_major: best.version_major,
            version_minor: best.version_minor,
            version_patch: best.version_patch,
            storage_path: best.storage_path,
            sha256: best.sha256,
            size_bytes: best.size_bytes,
            notes: best.notes,
          },
        });
      }

      case "firmware_url": {
        const releaseId = asString(params.release_id);
        if (!releaseId) {
          return envelope(apiVersion, false, null, "release_id_required", 400);
        }

        const { data: rel, error } = await admin
          .from("firmware_releases")
          .select("*")
          .eq("id", releaseId)
          .eq("is_active", true)
          .maybeSingle();
        if (error || !rel) {
          return envelope(apiVersion, false, null, "release_not_found", 404);
        }

        const ttl = Math.min(asNumber(params.expires_in) ?? 300, 3600);
        const { data: signed, error: signErr } = await admin.storage
          .from("firmware")
          .createSignedUrl(rel.storage_path, ttl);

        if (signErr || !signed?.signedUrl) {
          console.error("firmware_url:", signErr);
          return envelope(apiVersion, false, {
            message: "Signed URL failed; ensure Storage bucket 'firmware' exists",
            storage_path: rel.storage_path,
          }, "signed_url_failed", 500);
        }

        return envelope(apiVersion, true, {
          release_id: rel.id,
          url: signed.signedUrl,
          expires_in: ttl,
          storage_path: rel.storage_path,
          sha256: rel.sha256,
          size_bytes: rel.size_bytes,
          version_major: rel.version_major,
          version_minor: rel.version_minor,
          version_patch: rel.version_patch,
        });
      }

      default:
        return envelope(apiVersion, false, {
          message: "Unknown action",
          actions: [
            "list_active_sensors",
            "query",
            "upsert_device",
            "upsert_sensor",
            "insert_reading",
            "firmware_check",
            "firmware_url",
          ],
        }, "unknown_action", 400);
    }
  } catch (e) {
    console.error("device-api:", e);
    return envelope(apiVersion || 1, false, null, "internal_error", 500);
  }
});

// deno-lint-ignore no-explicit-any
function mapDeviceRow(r: any) {
  return {
    device_mac: r.device_mac,
    user_id: r.user_id,
    name: r.name ?? r.dev_name ?? "",
    dev_name: r.dev_name ?? r.name ?? "",
    device_ip: r.device_ip,
    dev_type: r.dev_type ?? 0,
    feature_mask: Number(r.feature_mask ?? 0),
    sending_int: r.sending_int ?? 0,
    firmware_major: r.firmware_major ?? 0,
    firmware_minor: r.firmware_minor ?? 0,
    firmware_patch: r.firmware_patch ?? 0,
    expired: !!r.expired,
    flags: r.flags ?? 0,
    is_active: !!r.is_active,
    data_received: isoToUnix(r.data_received),
    data_sent: isoToUnix(r.data_sent),
    last_seen_at: isoToUnix(r.last_seen_at),
  };
}

// deno-lint-ignore no-explicit-any
function mapSensorRow(r: any) {
  return {
    device_mac: r.device_mac,
    sns_type: r.sns_type,
    sns_id: r.sns_id,
    sns_name: r.sns_name ?? "",
    sns_value: r.sns_value ?? 0,
    time_read: isoToUnix(r.time_read),
    time_logged: isoToUnix(r.time_logged),
    sending_int: r.sending_int ?? 0,
    flags: r.flags ?? 0,
    expired: !!r.expired,
    limit_high: r.limit_high,
    limit_low: r.limit_low,
    utc_offset: r.utc_offset ?? 0,
  };
}

// deno-lint-ignore no-explicit-any
function mapReadingRow(r: any) {
  return {
    id: r.id,
    device_mac: r.device_mac,
    device_ip: r.device_ip,
    sns_type: r.sns_type,
    sns_id: r.sns_id,
    sns_name: r.sns_name ?? "",
    sns_value: r.sns_value,
    utc_offset: r.utc_offset ?? 0,
    time_logged: isoToUnix(r.time_logged),
    time_read: isoToUnix(r.time_read),
    flagged: !!r.flagged,
    expired: !!r.expired,
    critical: !!r.critical,
    sending_int: r.sending_int,
    flags: r.flags ?? 0,
  };
}
