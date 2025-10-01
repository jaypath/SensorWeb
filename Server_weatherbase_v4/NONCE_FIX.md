# Nonce Implementation Fix - ESPNOW WiFi Password Exchange

## Issue Identified
The ESPNOW WiFi password exchange protocol had an incomplete nonce implementation that could potentially allow replay attacks.

## Problem
- **Before**: Server received nonce in Type 2 request but **did not echo it back** in Type 3 response
- **Impact**: Client validation of nonce would always fail or pass trivially with zeros
- **Security risk**: Replay attacks possible (attacker could reuse captured requests)

## Fix Applied

### Files Modified
1. `Server_weatherbase_v4/src/AddESPNOW.cpp` - Line 277
2. `GLOBAL_LIBRARY/AddESPNOW.cpp` - Line 277

### Change Made
Added nonce echo-back after encrypting WiFi password:

```cpp
// Before (line 274):
memcpy(resp.payload, encrypted, (outlen < 48) ? outlen : 48);

// Added (line 277):
// Echo nonce back from request to response for replay attack prevention
memcpy(resp.payload + 32, msg.payload + 32, 8);
```

## How It Works Now

### Complete Flow

**1. Sensor Sends Type 2 (WIFI_PW_REQUEST)**
```
Payload (before LMK encryption):
[0-15]:   Random temp AES key
[16-31]:  Random IV
[32-39]:  Nonce (8 random bytes) ← Stored in I.WIFI_RECOVERY_NONCE
[40-79]:  Zeros
```

**2. Server Receives & Processes**
- Decrypts message with LMK → gets temp key, IV, nonce
- Encrypts WiFi password with temp key+IV
- **NOW: Copies nonce from request to response**

**3. Server Sends Type 3 (WIFI_PW_RESPONSE)**
```
Payload (before LMK encryption):
[0-31]:   WiFi password (encrypted with temp key+IV)
[32-39]:  Nonce (ECHOED from request) ← NEW!
[40-79]:  Additional data or zeros
```

**4. Sensor Validates**
- Decrypts message with LMK
- Decrypts WiFi password with temp key+IV
- **Checks nonce**: `if (I.WIFI_RECOVERY_NONCE[i] != msg.payload[32 + i])` → REJECT
- If all checks pass → Update WiFi credentials

## Security Benefits

### Replay Attack Prevention
**Before Fix**: 
- Attacker captures Type 2 request
- Attacker replays request hours/days later
- Server sends WiFi password again
- ❌ Attack succeeds

**After Fix**:
- Attacker captures Type 2 request
- Attacker replays request later
- Server echoes nonce from OLD request
- Sensor expects nonce from NEW request
- ✅ Validation fails, attack prevented

### Multi-Layer Validation

The WiFi password exchange now validates **4 security layers**:

1. ✅ **Time window**: TEMP_AES expires after 5 minutes
2. ✅ **MAC validation**: Response must come from expected server MAC
3. ✅ **Nonce validation**: Response nonce must match request (NEW!)
4. ✅ **Double encryption**: LMK + temporary session key

## Testing Recommendations

1. **Normal flow**: Verify WiFi password exchange still works
2. **Replay attack**: Capture and replay Type 2 → Should be rejected
3. **Timeout**: Wait 6 minutes after request → Should be rejected
4. **Wrong server**: Response from different MAC → Should be rejected
5. **Nonce mismatch**: Modify nonce in response → Should be rejected

## Backward Compatibility

⚠️ **Breaking Change**: 
- Old sensors (without nonce validation) will still work with new server
- New sensors require updated server that echoes nonce
- Mixed environments need coordinated update

## Nonce Lifecycle Management (ADDITIONAL FIX)

### Issue Found During Review
The nonce was being validated but **not consistently cleared** after use. This could allow nonce reuse in edge cases.

### Additional Cleanup Added
Nonce is now cleared in **all** cleanup paths:

**1. On Success** (line 335):
```cpp
memset(I.WIFI_RECOVERY_NONCE, 0, 8);
```

**2. On Validation Failure** (line 307):
```cpp
memset(I.WIFI_RECOVERY_NONCE, 0, 8);
```

**3. On Decryption Failure** (line 321):
```cpp
memset(I.WIFI_RECOVERY_NONCE, 0, 8);
```

**4. On WIFI_KEY_REQUIRED** (lines 369, 377) ← **NEW**:
```cpp
memset(I.WIFI_RECOVERY_NONCE, 0, 8);
```

**5. On TERMINATE** (line 391) ← **NEW**:
```cpp
memset(I.WIFI_RECOVERY_NONCE, 0, 8);
```

### Time Limit
- ✅ **5-minute expiration**: TEMP_AES_TIME is checked (line 288)
- ✅ **Immediate cleanup**: Nonce cleared on success or any error
- ✅ **One-time use**: Nonce cannot be reused after response received

### Complete Security Properties

| Property | Implementation | Status |
|----------|---------------|--------|
| **Nonce checked on receive** | Lines 297-302 | ✅ YES |
| **Cleared after success** | Line 335 | ✅ YES |
| **Cleared on failure** | Lines 307, 321 | ✅ YES |
| **Cleared on key refresh** | Line 377 | ✅ YES (NEW) |
| **Cleared on terminate** | Line 391 | ✅ YES (NEW) |
| **Time-limited** | 5 min expiration | ✅ YES |
| **MAC validated** | Lines 289-293 | ✅ YES |

## Status
- ✅ Code fixed in both Server_weatherbase_v4 and GLOBAL_LIBRARY
- ✅ Nonce echo-back implemented
- ✅ Nonce cleanup completed in all paths
- ✅ No linter errors
- ✅ Documentation updated
- ⏳ Needs physical testing with actual hardware

---
**Date**: 2025-10-01  
**Fixed by**: AI Assistant  
**Reviewed by**: [jsp]

