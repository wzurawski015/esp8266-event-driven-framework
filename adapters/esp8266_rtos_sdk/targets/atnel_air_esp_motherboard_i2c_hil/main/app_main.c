#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_log.h"

#include "board_profile.h"

#include "ev/compiler.h"
#include "ev/esp8266_i2c_hil.h"
#include "ev/esp8266_port_adapters.h"
#include "ev/mcp23008_actor.h"
#include "ev/rtc_actor.h"

#define EV_BOARD_TAG EV_BOARD_PROFILE_TAG
#define EV_BOARD_NAME EV_BOARD_PROFILE_NAME

EV_STATIC_ASSERT(EV_RTC_DEFAULT_ADDR_7BIT == EV_BOARD_RTC_ADDR_7BIT,
                 "RTC actor default address must stay fixed to the ATNEL board wiring");
EV_STATIC_ASSERT(EV_MCP23008_DEFAULT_ADDR_7BIT == EV_BOARD_MCP23008_ADDR_7BIT,
                 "MCP23008 actor default address must stay fixed to the ATNEL board wiring");

static ev_i2c_port_t s_board_i2c_port;
static ev_irq_port_t s_board_irq_port;

static const ev_gpio_irq_line_config_t k_board_irq_lines[] = {
    {
        .line_id = 0U,
        .gpio_num = EV_BOARD_IRQ_INT0_GPIO,
        .trigger = EV_GPIO_IRQ_TRIGGER_FALLING,
        .pull_mode = EV_GPIO_IRQ_PULL_UP,
    },
    {
        .line_id = 1U,
        .gpio_num = EV_BOARD_IRQ_IR0_GPIO,
        .trigger = EV_GPIO_IRQ_TRIGGER_ANYEDGE,
        .pull_mode = EV_GPIO_IRQ_PULL_UP,
    },
};

static void ev_hil_idle_forever(void)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void app_main(void)
{
    ev_i2c_port_t *runtime_i2c_port = NULL;
    ev_irq_port_t *runtime_irq_port = NULL;
    ev_result_t rc;

    (void)uart_set_baudrate(UART_NUM_0, 115200U);
    ESP_LOGI(EV_BOARD_TAG, "starting %s I2C zero-heap HIL", EV_BOARD_NAME);

    rc = ev_esp8266_i2c_port_init(&s_board_i2c_port, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    if (rc != EV_OK) {
        ESP_LOGE(EV_BOARD_TAG, "i2c adapter init failed rc=%d", (int)rc);
        ev_hil_idle_forever();
    }
    runtime_i2c_port = &s_board_i2c_port;

    rc = ev_esp8266_irq_port_init(&s_board_irq_port,
                                  k_board_irq_lines,
                                  (sizeof(k_board_irq_lines) / sizeof(k_board_irq_lines[0])));
    if (rc != EV_OK) {
        ESP_LOGW(EV_BOARD_TAG, "irq adapter init failed rc=%d; IRQ flood HIL will be skipped", (int)rc);
    } else {
        runtime_irq_port = &s_board_irq_port;
    }

    {
        const ev_esp8266_i2c_hil_config_t hil_cfg = {
            .suite_name = "atnel-i2c-zero-heap-hil",
            .board_tag = EV_BOARD_TAG,
            .i2c_port = runtime_i2c_port,
            .irq_port = runtime_irq_port,
            .i2c_port_num = EV_I2C_PORT_NUM_0,
            .rtc_addr_7bit = EV_BOARD_RTC_ADDR_7BIT,
            .mcp23008_addr_7bit = EV_BOARD_MCP23008_ADDR_7BIT,
            .oled_addr_7bit = EV_BOARD_OLED_ADDR_7BIT,
            .missing_addr_7bit = EV_BOARD_HIL_I2C_MISSING_ADDR_7BIT,
            .rtc_read_iterations = EV_BOARD_HIL_I2C_RTC_READ_ITERATIONS,
            .mcp_rw_iterations = EV_BOARD_HIL_I2C_MCP_RW_ITERATIONS,
            .oled_partial_flushes = EV_BOARD_HIL_I2C_OLED_PARTIAL_FLUSHES,
            .oled_full_flushes = EV_BOARD_HIL_I2C_OLED_FULL_FLUSHES,
            .irq_flood_i2c_transactions = EV_BOARD_HIL_I2C_IRQ_FLOOD_TRANSACTIONS,
            .sda_fault_gpio = EV_BOARD_HIL_I2C_SDA_FAULT_GPIO,
            .scl_fault_gpio = EV_BOARD_HIL_I2C_SCL_FAULT_GPIO,
            .irq_flood_output_gpio = EV_BOARD_HIL_IRQ_FLOOD_OUTPUT_GPIO,
            .irq_flood_line_id = EV_BOARD_HIL_IRQ_FLOOD_LINE_ID,
        };

        rc = ev_esp8266_i2c_zero_heap_hil_run(&hil_cfg);
        if (rc == EV_OK) {
            ESP_LOGI(EV_BOARD_TAG, "i2c zero-heap HIL completed successfully");
        } else {
            ESP_LOGE(EV_BOARD_TAG, "i2c zero-heap HIL failed rc=%d", (int)rc);
        }
    }

    ev_hil_idle_forever();
}
