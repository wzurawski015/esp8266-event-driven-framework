#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_log.h"

#include "board_profile.h"

#include "ev/esp8266_onewire_hil.h"
#include "ev/esp8266_port_adapters.h"

#define EV_BOARD_TAG EV_BOARD_PROFILE_TAG
#define EV_BOARD_NAME EV_BOARD_PROFILE_NAME

static ev_onewire_port_t s_board_onewire_port;
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
    ev_result_t rc;

    (void)uart_set_baudrate(UART_NUM_0, 115200U);
    ESP_LOGI(EV_BOARD_TAG, "starting %s OneWire IRQ HIL", EV_BOARD_NAME);

    rc = ev_esp8266_onewire_port_init(&s_board_onewire_port, EV_BOARD_ONEWIRE_GPIO);
    if (rc != EV_OK) {
        ESP_LOGE(EV_BOARD_TAG, "onewire adapter init failed rc=%d", (int)rc);
        ev_hil_idle_forever();
    }

    rc = ev_esp8266_irq_port_init(&s_board_irq_port,
                                  k_board_irq_lines,
                                  (sizeof(k_board_irq_lines) / sizeof(k_board_irq_lines[0])));
    if (rc != EV_OK) {
        ESP_LOGE(EV_BOARD_TAG, "irq adapter init failed rc=%d", (int)rc);
        ev_hil_idle_forever();
    }

    {
        const ev_esp8266_onewire_hil_config_t hil_cfg = {
            .suite_name = "atnel-onewire-irq-hil",
            .board_tag = EV_BOARD_TAG,
            .onewire_port = &s_board_onewire_port,
            .irq_port = &s_board_irq_port,
            .ds18b20_read_iterations = EV_BOARD_HIL_ONEWIRE_READS,
            .max_reset_critical_section_us = EV_BOARD_HIL_ONEWIRE_MAX_RESET_CRITICAL_US,
            .max_bit_critical_section_us = EV_BOARD_HIL_ONEWIRE_MAX_BIT_CRITICAL_US,
            .irq_flood_output_gpio = EV_BOARD_HIL_IRQ_FLOOD_OUTPUT_GPIO,
            .irq_flood_line_id = EV_BOARD_HIL_IRQ_FLOOD_LINE_ID,
        };

        rc = ev_esp8266_onewire_irq_hil_run(&hil_cfg);
        if (rc == EV_OK) {
            ESP_LOGI(EV_BOARD_TAG, "onewire IRQ HIL completed successfully");
        } else {
            ESP_LOGE(EV_BOARD_TAG, "onewire IRQ HIL failed rc=%d", (int)rc);
        }
    }

    ev_hil_idle_forever();
}
