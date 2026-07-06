#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "board_profile.h"

#include "ev/compiler.h"
#include "ev/demo_app.h"
#include "ev/esp8266_boot_diag.h"
#include "ev/esp8266_port_adapters.h"
#include "ev/esp8266_runtime_app.h"
#include "ev/port_gpio.h"

#define EV_BOARD_TAG EV_BOARD_PROFILE_TAG
#define EV_BOARD_NAME EV_BOARD_PROFILE_NAME

static ev_i2c_port_t s_board_i2c_port;
static ev_onewire_port_t s_board_onewire_port;
static ev_gpio_port_t s_board_gpio_port;
#if EV_BOARD_HAS_NET
static ev_net_port_t s_board_net_port;
#endif

#if EV_BOARD_HAS_NET
static const ev_esp8266_net_config_t k_board_net_cfg = {
    .wifi_ssid = EV_BOARD_NET_WIFI_SSID,
    .wifi_password = EV_BOARD_NET_WIFI_PASSWORD,
    .wifi_auth_mode = EV_BOARD_NET_WIFI_AUTH_MODE,
    .mqtt_broker_uri = EV_BOARD_NET_MQTT_BROKER_URI,
    .mqtt_client_id = EV_BOARD_NET_MQTT_CLIENT_ID,
};
#endif

static const ev_demo_app_board_profile_t k_atb_thermo_runtime_profile = {
    .capabilities_mask = (EV_BOARD_HAS_I2C0 ? EV_DEMO_APP_BOARD_CAP_I2C0 : 0U) |
                         (EV_BOARD_HAS_BH1750 ? EV_DEMO_APP_BOARD_CAP_BH1750 : 0U) |
                         (EV_BOARD_HAS_ONEWIRE0 ? EV_DEMO_APP_BOARD_CAP_ONEWIRE0 : 0U) |
                         (EV_BOARD_HAS_NET ? EV_DEMO_APP_BOARD_CAP_NET : 0U),
    .hardware_present_mask = EV_BOARD_RUNTIME_HARDWARE_PRESENT_MASK,
    .supervisor_required_mask = EV_BOARD_SUPERVISOR_REQUIRED_MASK,
    .supervisor_optional_mask = EV_BOARD_SUPERVISOR_OPTIONAL_MASK,
    .i2c_port_num = EV_I2C_PORT_NUM_0,
    .rtc_sqw_line_id = EV_BOARD_RTC_SQW_LINE_ID,
    .mcp23008_addr_7bit = EV_BOARD_MCP23008_ADDR_7BIT,
    .rtc_addr_7bit = EV_BOARD_RTC_ADDR_7BIT,
    .oled_addr_7bit = EV_BOARD_OLED_ADDR_7BIT,
    .bh1750_addr_7bit = EV_BOARD_BH1750_ADDR_7BIT,
    .oled_controller = EV_BOARD_OLED_CONTROLLER,
    .watchdog_timeout_ms = 0U,
    .remote_command_token = EV_BOARD_NET_COMMAND_TOKEN,
    .remote_command_capabilities = EV_BOARD_REMOTE_COMMAND_CAPABILITIES,
};

typedef struct ev_atb_i2c_probe {
    uint8_t addr7;
    const char *name;
    bool optional;
    bool deferred;
} ev_atb_i2c_probe_t;

static const ev_atb_i2c_probe_t k_atb_i2c_probes[] = {
    { EV_BOARD_BH1750_ADDR_7BIT, "BH1750", true, false },
    { EV_BOARD_BH1750_ALT_ADDR_7BIT, "BH1750_ALT", true, false },
    { EV_BOARD_OLED_ADDR_7BIT, "OLED", true, false },
    { EV_BOARD_BME280_BMP180_ADDR_7BIT, "BME280_BMP180", true, true },
    { EV_BOARD_BME280_BMP180_ALT_ADDR_7BIT, "BME280_BMP180_ALT", true, true },
};

static const char *ev_atb_i2c_status_cstr(ev_i2c_status_t status)
{
    switch (status) {
    case EV_I2C_OK:
        return "ACK";
    case EV_I2C_ERR_NACK:
        return "NACK";
    case EV_I2C_ERR_TIMEOUT:
        return "TIMEOUT";
    case EV_I2C_ERR_BUS_LOCKED:
        return "BUS_LOCKED";
    default:
        return "ERR";
    }
}

static ev_result_t ev_atb_gpio_configure_output(ev_gpio_port_t *port, ev_gpio_num_t pin, bool initial_high)
{
    const ev_gpio_config_t cfg = {
        .direction = EV_GPIO_OUTPUT,
        .pull = EV_GPIO_PULL_FLOATING,
        .open_drain = false,
        .initial_high = initial_high,
    };
    return port->configure(port->ctx, pin, &cfg);
}

static ev_result_t ev_atb_gpio_configure_input(ev_gpio_port_t *port, ev_gpio_num_t pin)
{
    const ev_gpio_config_t cfg = {
        .direction = EV_GPIO_INPUT,
        .pull = EV_GPIO_PULL_FLOATING,
        .open_drain = false,
        .initial_high = false,
    };
    return port->configure(port->ctx, pin, &cfg);
}

static ev_result_t ev_atb_thermo_gpio_ready(ev_gpio_port_t *port)
{
    ev_result_t rc;
    bool pir_level = false;
    const bool pwr_off_level = (EV_ATB_THERMO_PWR_CTRL_ACTIVE_LOW != 0U);
    const bool pwr_on_level = !pwr_off_level;

    if ((port == NULL) || (port->configure == NULL) || (port->write == NULL) || (port->read == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

#if EV_ATB_THERMO_ENABLE_PERIPH_POWER_CONTROL
    rc = ev_atb_gpio_configure_output(port, EV_BOARD_PWR_CTRL_GPIO, pwr_off_level);
    if (rc != EV_OK) {
        printf("EV_ATB_THERMO_PWR_CTRL gpio=%u active_low=%u state=CONFIG_FAILED rc=%d\n",
               (unsigned)EV_BOARD_PWR_CTRL_GPIO,
               (unsigned)EV_ATB_THERMO_PWR_CTRL_ACTIVE_LOW,
               (int)rc);
        return rc;
    }
    rc = port->write(port->ctx, EV_BOARD_PWR_CTRL_GPIO, pwr_on_level);
    if (rc != EV_OK) {
        printf("EV_ATB_THERMO_PWR_CTRL gpio=%u active_low=%u state=ON result=FAIL rc=%d\n",
               (unsigned)EV_BOARD_PWR_CTRL_GPIO,
               (unsigned)EV_ATB_THERMO_PWR_CTRL_ACTIVE_LOW,
               (int)rc);
        return rc;
    }
    printf("EV_ATB_THERMO_PWR_CTRL gpio=%u active_low=%u state=ON result=OK\n",
           (unsigned)EV_BOARD_PWR_CTRL_GPIO,
           (unsigned)EV_ATB_THERMO_PWR_CTRL_ACTIVE_LOW);
    vTaskDelay(pdMS_TO_TICKS(EV_ATB_THERMO_PERIPH_STABILIZE_MS));
    printf("EV_ATB_THERMO_PERIPH_POWER_STABLE wait_ms=%u\n", (unsigned)EV_ATB_THERMO_PERIPH_STABILIZE_MS);
#else
    printf("EV_ATB_THERMO_PWR_CTRL gpio=%u state=DISABLED result=SKIPPED\n", (unsigned)EV_BOARD_PWR_CTRL_GPIO);
#endif

    rc = ev_atb_gpio_configure_output(port, EV_BOARD_STATUS_LED_GPIO, true);
    if (rc == EV_OK) {
        printf("EV_ATB_THERMO_LED_READY gpio=%u active_low=1\n", (unsigned)EV_BOARD_STATUS_LED_GPIO);
    }
    if (rc != EV_OK) return rc;

    rc = ev_atb_gpio_configure_output(port, EV_BOARD_AUDIO_GPIO, false);
    if (rc == EV_OK) {
        printf("EV_ATB_THERMO_AUDIO_IDLE gpio=%u level=0\n", (unsigned)EV_BOARD_AUDIO_GPIO);
    }
    if (rc != EV_OK) return rc;

    rc = ev_atb_gpio_configure_output(port, EV_BOARD_PIR_DIS_GPIO, false);
    if (rc == EV_OK) {
        printf("EV_ATB_THERMO_PIR_DIS gpio=%u level=0 mode=safe_default\n", (unsigned)EV_BOARD_PIR_DIS_GPIO);
    }
    if (rc != EV_OK) return rc;

    rc = ev_atb_gpio_configure_input(port, EV_BOARD_PIR_CHECK_GPIO);
    if (rc != EV_OK) return rc;
    rc = port->read(port->ctx, EV_BOARD_PIR_CHECK_GPIO, &pir_level);
    if (rc != EV_OK) return rc;
    printf("EV_ATB_THERMO_PIR_CHECK gpio=%u level=%u\n",
           (unsigned)EV_BOARD_PIR_CHECK_GPIO,
           pir_level ? 1U : 0U);

    return EV_OK;
}

static void ev_atb_thermo_i2c_scan(ev_i2c_port_t *port)
{
    size_t i;

    if ((port == NULL) || (port->write_stream == NULL)) {
        printf("EV_ATB_THERMO_I2C_SCAN_SKIPPED reason=no_port\n");
        return;
    }

    printf("EV_ATB_THERMO_I2C_SCAN_BEGIN speed_hz=%u\n", (unsigned)EV_BOARD_I2C_DEFAULT_SPEED_HZ);
    for (i = 0U; i < (sizeof(k_atb_i2c_probes) / sizeof(k_atb_i2c_probes[0])); ++i) {
        const ev_atb_i2c_probe_t *probe = &k_atb_i2c_probes[i];
        const ev_i2c_status_t status = port->write_stream(port->ctx,
                                                          EV_I2C_PORT_NUM_0,
                                                          probe->addr7,
                                                          NULL,
                                                          0U);
        printf("EV_ATB_THERMO_I2C_SCAN addr=0x%02x name=%s status=%s optional=%u deferred=%u\n",
               (unsigned)probe->addr7,
               probe->name,
               ev_atb_i2c_status_cstr(status),
               probe->optional ? 1U : 0U,
               probe->deferred ? 1U : 0U);
    }
    printf("EV_ATB_THERMO_I2C_SCAN_END\n");
}

void app_main(void)
{
    static const ev_boot_diag_config_t k_boot_diag = {
        .board_tag = EV_BOARD_TAG,
        .board_name = EV_BOARD_NAME,
        .uart_port = 0U,
        .uart_baud_rate = 115200U,
        .heartbeat_period_ms = 1000U,
    };
    ev_i2c_port_t *runtime_i2c_port = NULL;
    ev_onewire_port_t *runtime_onewire_port = NULL;
    ev_net_port_t *runtime_net_port = NULL;
    ev_result_t rc;

    (void)uart_set_baudrate(UART_NUM_0, 115200U);

    printf("EV_ATB_THERMO_BOOT target=atb_thermo_wemos_esp_wroom_02_4mb board=%s flash=4mb\n", EV_BOARD_NAME);
    printf("EV_ATB_THERMO_PIN_MAP scl=%u sda=%u onewire=%u pwr=%u led=%u pir_check=%u audio=%u pir_dis=%u\n",
           (unsigned)EV_BOARD_I2C_SCL_GPIO,
           (unsigned)EV_BOARD_I2C_SDA_GPIO,
           (unsigned)EV_BOARD_ONEWIRE_GPIO,
           (unsigned)EV_BOARD_PWR_CTRL_GPIO,
           (unsigned)EV_BOARD_STATUS_LED_GPIO,
           (unsigned)EV_BOARD_PIR_CHECK_GPIO,
           (unsigned)EV_BOARD_AUDIO_GPIO,
           (unsigned)EV_BOARD_PIR_DIS_GPIO);
    printf("EV_ATB_THERMO_JP2_REQUIRED position=2-3 mode=normal_pwr_ctrl\n");

    rc = ev_esp8266_gpio_port_init(&s_board_gpio_port);
    if (rc == EV_OK) {
        rc = ev_atb_thermo_gpio_ready(&s_board_gpio_port);
    }
    if (rc != EV_OK) {
        printf("EV_ATB_THERMO_GPIO_INIT_FAILED rc=%d\n", (int)rc);
    }

    rc = ev_esp8266_i2c_port_init(&s_board_i2c_port, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    if (rc == EV_OK) {
        runtime_i2c_port = &s_board_i2c_port;
        printf("EV_ATB_THERMO_I2C_READY port=0 scl=%u sda=%u speed_hz=%u\n",
               (unsigned)EV_BOARD_I2C_SCL_GPIO,
               (unsigned)EV_BOARD_I2C_SDA_GPIO,
               (unsigned)EV_BOARD_I2C_DEFAULT_SPEED_HZ);
        ev_atb_thermo_i2c_scan(runtime_i2c_port);
    } else {
        printf("EV_ATB_THERMO_I2C_INIT_FAILED rc=%d\n", (int)rc);
    }

    rc = ev_esp8266_onewire_port_init(&s_board_onewire_port, EV_BOARD_ONEWIRE_GPIO);
    if (rc == EV_OK) {
        runtime_onewire_port = &s_board_onewire_port;
        printf("EV_ATB_THERMO_ONEWIRE_READY dq=%u\n", (unsigned)EV_BOARD_ONEWIRE_GPIO);
    } else {
        printf("EV_ATB_THERMO_ONEWIRE_INIT_FAILED rc=%d\n", (int)rc);
    }

#if EV_BOARD_HAS_NET
    rc = ev_esp8266_net_port_init(&s_board_net_port, &k_board_net_cfg);
    if (rc == EV_OK) {
        runtime_net_port = &s_board_net_port;
    } else {
        printf("EV_ATB_THERMO_NET_PORT_INIT_FAILED rc=%d\n", (int)rc);
    }
#endif

    printf("EV_ATB_THERMO_RUNTIME_READY\n");
    ev_esp8266_runtime_app_run(&k_boot_diag,
                               runtime_i2c_port,
                               NULL,
                               runtime_onewire_port,
                               NULL,
                               runtime_net_port,
                               &k_atb_thermo_runtime_profile);
}
