# Weather Optimization Rollback Guide

## Quick Rollback (Recommended)

### Step 1: Disable Optimizations
Edit `platformio.ini` and comment out the weather optimization flags:

```ini
build_flags = 
	-DBOARD_HAS_PSRAM
    -DCONFIG_SPIRAM_SUPPORT=1
	-mfix-esp32-psram-cache-issue
	-I lib
	-D PLUS=1
	-D LV_MEM_SIZE="(96U * 1024U)"
	; WEATHER OPTIMIZATION FLAGS - REMOVE THESE LINES TO DISABLE OPTIMIZATIONS
	; -DWEATHER_OPTIMIZATION_ENABLED=1
	; -DPARALLEL_REQUESTS_ENABLED=1
	; -DWEATHER_CACHE_SIZE=3
	; -DMAX_RETRY_ATTEMPTS=3
	; END WEATHER OPTIMIZATION FLAGS
```

### Step 2: Rebuild
```bash
pio run -e wthrbase3
```

This will automatically revert to the original weather system.

## Complete Rollback (If Issues Persist)

### Step 1: Remove Optimization Files
```bash
rm src/Weather_Optimized.hpp
rm src/Weather_Optimized.cpp
```

### Step 2: Restore globals.hpp
Edit `src/globals.hpp` and remove these lines:
```cpp
// Remove these lines:
// #ifdef WEATHER_OPTIMIZATION_ENABLED
//     #include <Weather_Optimized.hpp>
// #endif
```

### Step 3: Restore globals.cpp
Edit `src/globals.cpp` and change:
```cpp
// Change from:
#ifdef WEATHER_OPTIMIZATION_ENABLED
    WeatherInfoOptimized WeatherData;  // Optimized weather class
#else
    // WeatherInfo WeatherData;  // Original weather class (commented out)
#endif

// To:
WeatherInfo WeatherData;  // Original weather class
```

### Step 4: Restore main.cpp
Edit `src/main.cpp` and change:
```cpp
// Change from:
#ifdef WEATHER_OPTIMIZATION_ENABLED
    WeatherData.updateWeatherOptimized(3600);  // Optimized weather update
#else
    // WeatherData.updateWeather(3600);  // Original weather update (commented out)
#endif

// To:
WeatherData.updateWeather(3600);  // Original weather update
```

### Step 5: Rebuild
```bash
pio run -e wthrbase3
```

## Verification

After rollback, verify the system is using the original weather code:

1. **Check compilation**: Should compile without errors
2. **Check weather updates**: Should use original `updateWeather()` method
3. **Check performance**: Response times will return to original 8-12 seconds
4. **Check logs**: Should see original weather update messages

## Troubleshooting Rollback

### If Compilation Fails
1. Clean build directory: `pio run -t clean`
2. Rebuild: `pio run -e wthrbase3`

### If Weather Still Uses Optimizations
1. Check that all optimization flags are commented out
2. Verify `WEATHER_OPTIMIZATION_ENABLED` is not defined
3. Clean and rebuild

### If Original Files Are Missing
The original `Weather.hpp` and `Weather.cpp` files are preserved and unchanged. If they appear to be missing, they should still be in the `src/` directory.

## Emergency Rollback

If you need to quickly revert without editing files:

```bash
# Disable optimizations via command line
pio run -e wthrbase3 -DWEATHER_OPTIMIZATION_ENABLED=0
```

This will build without optimizations even if the flags are still in platformio.ini.

## Notes

- The original `Weather.hpp` and `Weather.cpp` files are **never modified**
- All optimizations are additive and non-destructive
- The system maintains full backward compatibility
- Rollback can be performed at any time without data loss 