COMPONENT_SRCDIRS := . \
    ../../../../core/src \
    ../../../../runtime/src \
    ../../../../modules/src \
    ../../../../drivers/src \
    ../../../../apps/demo
# ev_freertos_static_hooks.c is compiled from the local ev_platform source
# directory above. It provides FreeRTOS Idle/Timer static task memory hooks when
# the ESP8266 SDK FreeRTOSConfig.h enables configSUPPORT_STATIC_ALLOCATION.
COMPONENT_ADD_INCLUDEDIRS := include \
    ../../../../ports/include \
    ../../../../core/include \
    ../../../../runtime/include \
    ../../../../modules/include \
    ../../../../drivers/include \
    ../../../../core/generated/include \
    ../../../../apps/demo/include \
    ../../../../config
# ev_net_adapter.c is compiled from the local ev_platform source directory.
# It defaults to WiFi-only mode; define EV_ESP8266_NET_ENABLE_MQTT=1 in a
# target-specific build only after verifying the ESP8266 MQTT SDK component.
# Optional physical MQTT SDK build gate.  The default is WiFi-only and does not
# require mqtt_client.h because ev_net_adapter.c defaults
# EV_ESP8266_NET_ENABLE_MQTT to 0 internally.
ifneq ($(EV_ESP8266_NET_ENABLE_MQTT),)
CFLAGS += -DEV_ESP8266_NET_ENABLE_MQTT=$(EV_ESP8266_NET_ENABLE_MQTT)
endif
