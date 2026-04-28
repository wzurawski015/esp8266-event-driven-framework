#ifndef EV_ATNEL_AIR_BOARD_SECRETS_EXAMPLE_H
#define EV_ATNEL_AIR_BOARD_SECRETS_EXAMPLE_H

/*
 * Copy this file to board_secrets.local.h and keep the local file untracked.
 * Then build with EV_BOARD_INCLUDE_LOCAL_SECRETS defined, or provide equivalent
 * -D overrides from your build environment.
 *
 * The values below are placeholders only. Do not commit private credentials.
 */

#define EV_BOARD_HAS_NET 1U
#define EV_BOARD_NET_WIFI_SSID "YOUR_WIFI_SSID"
#define EV_BOARD_NET_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define EV_BOARD_NET_WIFI_AUTH_MODE EV_BOARD_NET_WIFI_AUTH_WPA2_PSK
#define EV_BOARD_NET_WIFI_SECURITY_LABEL "WPA2-PSK"

/* Leave empty for WiFi-only tests. Provide a broker URI only when MQTT HIL is
 * intentionally enabled in a later stage.
 */
#define EV_BOARD_NET_MQTT_BROKER_URI ""
#define EV_BOARD_NET_MQTT_CLIENT_ID EV_BOARD_PROFILE_TAG

/* Optional remote command dispatcher. Leave token empty and capabilities zero
 * unless MQTT command HIL is intentionally enabled.
 */
#define EV_BOARD_NET_COMMAND_TOKEN "YOUR_COMMAND_TOKEN"
#define EV_BOARD_REMOTE_COMMAND_CAPABILITIES 0U

#endif /* EV_ATNEL_AIR_BOARD_SECRETS_EXAMPLE_H */
