#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { ZAM_PROGRAM_IDLE, ZAM_PROGRAM_STARTING, ZAM_PROGRAM_RUNNING, ZAM_PROGRAM_STOPPING, ZAM_PROGRAM_CLEANUP } zam_program_state_t;
typedef enum { ZAM_PROGRAM_NONE, ZAM_PROGRAM_OK, ZAM_PROGRAM_STOPPED, ZAM_PROGRAM_SYNTAX_ERROR, ZAM_PROGRAM_RUNTIME_ERROR, ZAM_PROGRAM_MEMORY_ERROR, ZAM_PROGRAM_LOAD_ERROR, ZAM_PROGRAM_INTERNAL_ERROR } zam_program_result_t;
typedef struct {
    zam_program_state_t state;
    zam_program_result_t last_result;
    size_t lua_peak, lua_live, stack_free, output_dropped, output_produced;
    size_t heap_before, heap_after, heap_minimum, heap_largest;
    int64_t load_us, run_us, cleanup_us, stop_us;
} zam_program_status_t;
/* Internal sequence API. Boot uses prepare/launch; no persisted state. */
#define ZAM_AUTORUN_MAX_SCRIPTS 32
#define ZAM_AUTORUN_NAME_MAX 63
#define ZAM_AUTORUN_MAX_ENTRIES 256
typedef enum { ZAM_AUTORUN_IDLE, ZAM_AUTORUN_WAITING, ZAM_AUTORUN_SCANNING, ZAM_AUTORUN_RUNNING,
               ZAM_AUTORUN_COMPLETED, ZAM_AUTORUN_CANCELLED, ZAM_AUTORUN_FAILED } zam_autorun_state_t;
typedef struct {
    zam_autorun_state_t state;
    unsigned discovered, completed;
    char current[ZAM_AUTORUN_NAME_MAX+1];
    zam_program_result_t result;
    char message[64];
} zam_autorun_status_t;
esp_err_t zam_program_autorun_prepare(void);
esp_err_t zam_program_autorun_launch(void);
void zam_program_autorun_abort_prepared(void);
esp_err_t zam_program_autorun_start(void);
esp_err_t zam_program_autorun_cancel(void);
esp_err_t zam_program_autorun_status(zam_autorun_status_t *status);
bool zam_program_autorun_busy(void);
/* Reserve directory mutation atomically against sequence ownership. */
esp_err_t zam_program_autorun_mutation_begin(void);
void zam_program_autorun_mutation_end(void);
esp_err_t zam_program_init(void);
/* Called after the LAB has started; does not overwrite a run or earlier result. */
void zam_program_indicator_ready(void);
esp_err_t zam_program_run(const char *path);
esp_err_t zam_program_stop(void);
esp_err_t zam_program_get_status(zam_program_status_t *status);
esp_err_t zam_program_register_console(void);
/* Independent cursors; bytes and metadata are copied as one snapshot. */
typedef struct {
    uint64_t run_id, oldest, end, next, lost;
    size_t copied;
    bool exhausted;
} zam_program_output_t;
esp_err_t zam_program_output_read(uint64_t cursor,void *destination,size_t capacity,zam_program_output_t *result);
