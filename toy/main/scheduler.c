#include "scheduler.h"

#include "esp_timer.h"

static esp_timer_handle_t s_timer_handle = NULL;
static void (*s_task)(void*) = NULL;
static void* s_user_ctx = NULL;

static void timer_callback(void* arg) {
    if (s_task != NULL) {
        s_task(s_user_ctx);
    }
}

esp_err_t delayed_execute(void (*task)(void*), void* user_ctx, size_t delayed_us) {
    if (s_timer_handle == NULL) {
        esp_timer_create_args_t config = {
            .callback = timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "one_shot_timer",
        };

        esp_timer_create(&config, &s_timer_handle);
    }

    if (esp_timer_is_active(s_timer_handle)) {
        return ESP_ERR_INVALID_STATE;
    }

    s_task = task;
    s_user_ctx = user_ctx;

    return esp_timer_start_once(s_timer_handle, delayed_us);
}

esp_err_t delayed_execute_cancel();
