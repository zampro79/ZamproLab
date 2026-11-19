#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Internal firmware API: execute synchronously only in the owning runtime task. */
typedef enum {
    ZAM_LUA_OK, ZAM_LUA_STOPPED, ZAM_LUA_SYNTAX, ZAM_LUA_RUNTIME,
    ZAM_LUA_MEMORY, ZAM_LUA_LOAD, ZAM_LUA_INTERNAL
} zam_lua_result_t;
typedef struct {
    bool (*stop_requested)(void *context);
    void (*running)(void *context);
    void (*cleanup)(void *context);
    void (*output)(void *context, const char *bytes, size_t length);
    void *context;
} zam_lua_callbacks_t;
typedef struct {
    size_t lua_peak, lua_live, heap_before, heap_after, heap_minimum, heap_largest;
    int64_t load_us, run_us, cleanup_us;
} zam_lua_metrics_t;
zam_lua_result_t zam_runtime_lua_execute(const char *path, const zam_lua_callbacks_t *callbacks, zam_lua_metrics_t *metrics);
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
void zam_lua_psram_diagnostics(unsigned *live, unsigned *peak, unsigned *nonexternal);
#endif
