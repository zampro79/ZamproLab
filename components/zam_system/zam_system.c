#include "zam_system.h"
#include <inttypes.h>
#include <stdio.h>
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
#include "zam_runtime_lua.h"
#endif

#define VERSION (esp_app_get_description()->version)
esp_err_t zam_system_get_info(zam_system_info_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_chip_info_t chip;
    uint32_t flash;
    esp_chip_info(&chip);
    esp_err_t err=esp_flash_get_size(NULL,&flash);
    if (err!=ESP_OK) return err;
    *out=(zam_system_info_t){.name=ZAM_SYSTEM_NAME,.version=VERSION,.target=CONFIG_IDF_TARGET,
        .idf_version=esp_get_idf_version(),.uptime_ms=esp_timer_get_time()/1000,
        .flash_size=flash,.cores=chip.cores,.revision=chip.revision};
    return ESP_OK;
}
esp_err_t zam_system_get_memory(zam_system_memory_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=(zam_system_memory_t){
        .heap_free=heap_caps_get_free_size(MALLOC_CAP_8BIT),
        .heap_internal_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        .heap_internal_largest_block=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        .heap_minimum_free=heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        .heap_external_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)};
    return ESP_OK;
}
static int no_args(int argc, const char *name)
{
    if (argc == 1) return 1;
    printf("Usage: %s\n", name);
    return 0;
}
static int version_cmd(int argc, char **argv)
{
    if (!no_args(argc, argv[0])) return 1;
    printf("ZamproLab %s; ESP-IDF %s; target %s\n", VERSION, esp_get_idf_version(), CONFIG_IDF_TARGET);
    return 0;
}
static int info_cmd(int argc, char **argv)
{
    if (!no_args(argc, argv[0])) return 1;
    esp_chip_info_t chip;
    uint32_t flash;
    esp_chip_info(&chip);
    printf("Chip: %s, %d cores, revision %d.%d; CPU: %d MHz\n", CONFIG_IDF_TARGET, chip.cores, chip.revision / 100, chip.revision % 100, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    if (esp_flash_get_size(NULL, &flash) == ESP_OK) printf("Flash: %" PRIu32 " bytes\n", flash);
    else puts("Flash size unavailable");
#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) printf("PSRAM initialized: %zu bytes\n", esp_psram_get_size());
    else puts("PSRAM not initialized");
#else
    puts("PSRAM support disabled; presence unknown");
#endif
    return 0;
}
static int mem_cmd(int argc, char **argv)
{
    if (!no_args(argc, argv[0])) return 1;
    printf("Heap free: %zu bytes; internal free: %zu bytes; largest internal block: %zu bytes\n",
           heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    printf("External heap free: %zu bytes; minimum heap free: %zu bytes\n",
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
#if CONFIG_SPIRAM && CONFIG_IDF_TARGET_ESP32S3
    const uint32_t internal=MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t external=MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    printf("Experimental PSRAM: initialized=%s; physical=%zu; heap total=%zu; free=%zu; minimum=%zu; largest=%zu bytes\n",
           esp_psram_is_initialized() ? "yes" : "no", esp_psram_is_initialized() ? esp_psram_get_size() : 0,
           heap_caps_get_total_size(external), heap_caps_get_free_size(external),
           heap_caps_get_minimum_free_size(external), heap_caps_get_largest_free_block(external));
    printf("Experimental internal heap: free=%zu; minimum=%zu; largest=%zu bytes\n",
           heap_caps_get_free_size(internal), heap_caps_get_minimum_free_size(internal),
           heap_caps_get_largest_free_block(internal));
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
    unsigned live,peak,nonexternal;
    zam_lua_psram_diagnostics(&live,&peak,&nonexternal);
    printf("Experimental Lua: live=%u; peak=%u; nonexternal allocations=%u\n",live,peak,nonexternal);
#endif
#endif
    return 0;
}
static int uptime_cmd(int argc, char **argv)
{
    if (!no_args(argc, argv[0])) return 1;
    printf("Uptime: %" PRId64 " seconds\n", esp_timer_get_time() / 1000000);
    return 0;
}
static int reboot_cmd(int argc, char **argv)
{
    if (!no_args(argc, argv[0])) return 1;
    puts("Rebooting...");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}
void zam_system_log_startup(void)
{
    ESP_LOGI("ZamproLab", "ZamproLab %s - ESP-IDF %s - target %s", VERSION, esp_get_idf_version(), CONFIG_IDF_TARGET);
    esp_chip_info_t chip;
    uint32_t flash;
    esp_chip_info(&chip);
    ESP_LOGI("ZamproLab", "Chip: %d cores, revision %d.%d, features 0x%08" PRIx32,
             chip.cores, chip.revision / 100, chip.revision % 100, chip.features);
    if (esp_flash_get_size(NULL, &flash) == ESP_OK) ESP_LOGI("ZamproLab", "Flash: %" PRIu32 " bytes", flash);
    else ESP_LOGW("ZamproLab", "Flash size unavailable");
    ESP_LOGI("ZamproLab", "Heap free: %zu; internal free: %zu; largest internal block: %zu bytes",
             heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) ESP_LOGI("ZamproLab", "PSRAM initialized: %zu bytes", esp_psram_get_size());
    else ESP_LOGI("ZamproLab", "PSRAM not initialized");
#else
    ESP_LOGI("ZamproLab", "PSRAM support disabled; presence unknown");
#endif
}
esp_err_t zam_system_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command="version", .help="Show firmware version and target", .func=version_cmd},
        {.command="info", .help="Show chip, flash and PSRAM", .func=info_cmd},
        {.command="mem", .help="Show heap diagnostics", .func=mem_cmd},
        {.command="uptime", .help="Show time since boot", .func=uptime_cmd},
        {.command="reboot", .help="Restart the device", .func=reboot_cmd},
    };
    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); ++i) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
