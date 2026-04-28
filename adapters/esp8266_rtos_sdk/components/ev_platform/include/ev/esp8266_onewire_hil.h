#ifndef EV_ESP8266_ONEWIRE_HIL_H
#define EV_ESP8266_ONEWIRE_HIL_H

#include <stdint.h>

#include "ev/port_irq.h"
#include "ev/port_onewire.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_ESP8266_ONEWIRE_HIL_GPIO_DISABLED (-1)

typedef struct ev_esp8266_onewire_hil_config {
    const char *suite_name;
    const char *board_tag;
    ev_onewire_port_t *onewire_port;
    ev_irq_port_t *irq_port;
    uint32_t ds18b20_read_iterations;
    uint32_t max_reset_critical_section_us;
    uint32_t max_bit_critical_section_us;
    int irq_flood_output_gpio;
    ev_irq_line_id_t irq_flood_line_id;
} ev_esp8266_onewire_hil_config_t;

ev_result_t ev_esp8266_onewire_irq_hil_run(const ev_esp8266_onewire_hil_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* EV_ESP8266_ONEWIRE_HIL_H */
