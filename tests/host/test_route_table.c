#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "ev/actor_catalog.h"
#include "ev/event_catalog.h"
#include "ev/route_table.h"

int main(void)
{
    size_t i;
    size_t j;
    ev_route_span_t span;

    assert(ev_route_count() == 53U);
    assert(ev_route_count_for_event(EV_BOOT_STARTED) == 1U);
    assert(ev_route_count_for_event(EV_BOOT_COMPLETED) == 7U);
    assert(ev_route_count_for_event(EV_TICK_1S) == 9U);
    assert(ev_route_count_for_event(EV_TICK_100MS) == 3U);
    assert(ev_route_count_for_event(EV_GPIO_IRQ) == 2U);
    assert(ev_route_count_for_event(EV_TIME_UPDATED) == 2U);
    assert(ev_route_count_for_event(EV_TEMP_UPDATED) == 2U);
    assert(ev_route_count_for_event(EV_MCP23008_INPUT_CHANGED) == 2U);
    assert(ev_route_count_for_event(EV_BUTTON_EVENT) == 1U);
    assert(ev_route_count_for_event(EV_MCP23008_READY) == 3U);
    assert(ev_route_count_for_event(EV_PANEL_LED_SET_CMD) == 1U);
    assert(ev_route_count_for_event(EV_STREAM_CHUNK_READY) == 1U);
    assert(ev_route_count_for_event(EV_DIAG_SNAPSHOT_REQ) == 1U);
    assert(ev_route_count_for_event(EV_DIAG_SNAPSHOT_RSP) == 1U);
    assert(ev_route_count_for_event(EV_OLED_DISPLAY_TEXT_CMD) == 1U);
    assert(ev_route_count_for_event(EV_OLED_COMMIT_FRAME) == 1U);
    assert(ev_route_count_for_event(EV_RTC_READY) == 1U);
    assert(ev_route_count_for_event(EV_OLED_READY) == 1U);
    assert(ev_route_count_for_event(EV_DS18B20_READY) == 1U);
    assert(ev_route_count_for_event(EV_SYSTEM_READY) == 1U);
    assert(ev_route_count_for_event(EV_SYS_GOTO_SLEEP_CMD) == 1U);
    assert(ev_route_count_for_event(EV_NET_WIFI_UP) == 1U);
    assert(ev_route_count_for_event(EV_NET_WIFI_DOWN) == 1U);
    assert(ev_route_count_for_event(EV_NET_MQTT_UP) == 1U);
    assert(ev_route_count_for_event(EV_NET_MQTT_DOWN) == 1U);
    assert(ev_route_count_for_event(EV_NET_MQTT_MSG_RX) == 2U);
    assert(ev_route_count_for_event(EV_NET_MQTT_MSG_RX_LEASE) == 2U);
    assert(ev_route_count_for_event(EV_NET_TX_CMD) == 1U);
    assert(ev_route_count_for_event(EV_FAULT_REPORTED) == 1U);

    span = ev_route_span_for_event(EV_BOOT_COMPLETED);
    assert(span.count == 7U);
    assert(span.start_index < ev_route_count());
    for (i = 0U; i < span.count; ++i) {
        const ev_route_t *route = ev_route_at(span.start_index + i);
        assert(route != NULL);
        assert(route->event_id == EV_BOOT_COMPLETED);
    }
    span = ev_route_span_for_event((ev_event_id_t)EV_EVENT_COUNT);
    assert(span.start_index == 0U);
    assert(span.count == 0U);

    assert(ev_route_exists(EV_BOOT_COMPLETED, ACT_DIAG));
    assert(ev_route_exists(EV_BOOT_COMPLETED, ACT_APP));
    assert(ev_route_exists(EV_BOOT_COMPLETED, ACT_MCP23008));
    assert(ev_route_exists(EV_BOOT_COMPLETED, ACT_DS18B20));
    assert(ev_route_exists(EV_BOOT_COMPLETED, ACT_OLED));
    assert(ev_route_exists(EV_BOOT_COMPLETED, ACT_RTC));
    assert(ev_route_exists(EV_BOOT_COMPLETED, ACT_SUPERVISOR));

    assert(ev_route_exists(EV_TICK_1S, ACT_DIAG));
    assert(ev_route_exists(EV_TICK_1S, ACT_APP));
    assert(ev_route_exists(EV_TICK_1S, ACT_DS18B20));
    assert(ev_route_exists(EV_TICK_1S, ACT_OLED));
    assert(ev_route_exists(EV_TICK_1S, ACT_RTC));
    assert(ev_route_exists(EV_TICK_1S, ACT_SUPERVISOR));
    assert(ev_route_exists(EV_TICK_1S, ACT_WATCHDOG));
    assert(ev_route_exists(EV_TICK_1S, ACT_NETWORK));
    assert(ev_route_exists(EV_TICK_1S, ACT_COMMAND));

    assert(ev_route_exists(EV_TICK_100MS, ACT_DIAG));
    assert(ev_route_exists(EV_TICK_100MS, ACT_PANEL));
    assert(ev_route_exists(EV_TICK_100MS, ACT_MCP23008));
    assert(ev_route_exists(EV_GPIO_IRQ, ACT_DIAG));
    assert(ev_route_exists(EV_GPIO_IRQ, ACT_RTC));
    assert(ev_route_exists(EV_MCP23008_READY, ACT_DIAG));
    assert(ev_route_exists(EV_MCP23008_READY, ACT_RTC));
    assert(ev_route_exists(EV_MCP23008_READY, ACT_SUPERVISOR));
    assert(ev_route_exists(EV_RTC_READY, ACT_SUPERVISOR));
    assert(ev_route_exists(EV_OLED_READY, ACT_SUPERVISOR));
    assert(ev_route_exists(EV_DS18B20_READY, ACT_SUPERVISOR));
    assert(ev_route_exists(EV_SYSTEM_READY, ACT_APP));
    assert(ev_route_exists(EV_SYS_GOTO_SLEEP_CMD, ACT_POWER));
    assert(ev_route_exists(EV_NET_WIFI_UP, ACT_NETWORK));
    assert(ev_route_exists(EV_NET_WIFI_DOWN, ACT_NETWORK));
    assert(ev_route_exists(EV_NET_MQTT_UP, ACT_NETWORK));
    assert(ev_route_exists(EV_NET_MQTT_DOWN, ACT_NETWORK));
    assert(ev_route_exists(EV_NET_MQTT_MSG_RX, ACT_NETWORK));
    assert(ev_route_exists(EV_NET_MQTT_MSG_RX, ACT_COMMAND));
    assert(ev_route_exists(EV_NET_MQTT_MSG_RX_LEASE, ACT_NETWORK));
    assert(ev_route_exists(EV_NET_MQTT_MSG_RX_LEASE, ACT_COMMAND));
    assert(ev_route_exists(EV_NET_TX_CMD, ACT_NETWORK));
    assert(ev_route_exists(EV_FAULT_REPORTED, ACT_FAULT));
    assert(ev_route_exists(EV_PANEL_LED_SET_CMD, ACT_MCP23008));
    assert(ev_route_exists(EV_TIME_UPDATED, ACT_APP));
    assert(ev_route_exists(EV_TIME_UPDATED, ACT_NETWORK));
    assert(ev_route_exists(EV_TEMP_UPDATED, ACT_APP));
    assert(ev_route_exists(EV_TEMP_UPDATED, ACT_NETWORK));
    assert(ev_route_exists(EV_MCP23008_INPUT_CHANGED, ACT_PANEL));
    assert(ev_route_exists(EV_MCP23008_INPUT_CHANGED, ACT_NETWORK));
    assert(ev_route_exists(EV_BUTTON_EVENT, ACT_APP));
    assert(ev_route_exists(EV_DIAG_SNAPSHOT_RSP, ACT_APP));
    assert(ev_route_exists(EV_OLED_DISPLAY_TEXT_CMD, ACT_OLED));
    assert(ev_route_exists(EV_OLED_COMMIT_FRAME, ACT_OLED));
    assert(!ev_route_exists(EV_BOOT_STARTED, ACT_STREAM));

    for (i = 0U; i < ev_route_count(); ++i) {
        const ev_route_t *route = ev_route_at(i);
        assert(route != NULL);
        assert(ev_event_id_is_valid(route->event_id));
        assert(ev_actor_id_is_valid(route->target_actor));
    }

    for (i = 0U; i < ev_route_count(); ++i) {
        const ev_route_t *lhs = ev_route_at(i);
        assert(lhs != NULL);
        for (j = i + 1U; j < ev_route_count(); ++j) {
            const ev_route_t *rhs = ev_route_at(j);
            assert(rhs != NULL);
            assert(!((lhs->event_id == rhs->event_id) && (lhs->target_actor == rhs->target_actor)));
        }
    }

    return 0;
}
