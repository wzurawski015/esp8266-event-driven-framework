# Host-side USB-UART metadata placeholder for the Adafruit Feather HUZZAH ESP8266 BSP.
# This repository snapshot does not yet provide a buildable SDK Makefile for this target.
# Common Adafruit Feather HUZZAH boards use a CP2104/CP210x-class USB-UART;
# verify the actual VID/PID on your fixture and use a local override if needed.
EV_TARGET_NAME="adafruit_feather_huzzah_esp8266"
EV_TARGET_USB_UART_BRIDGE="CP2104 / Silicon Labs CP210x USB-UART (common, verify per board)"
EV_TARGET_USB_VID="10c4"
EV_TARGET_USB_PID="ea60"
EV_TARGET_USB_SERIAL=""
EV_TARGET_SERIAL_BY_ID_PATTERN="*/usb-Silicon_Labs*CP210*"
EV_TARGET_FLASH_BAUD="115200"
EV_TARGET_MONITOR_BAUD="115200"
EV_TARGET_RESET_MODE="default_reset"
EV_TARGET_FLASH_BEFORE="default_reset"
EV_TARGET_FLASH_AFTER="hard_reset"
EV_TARGET_PORT_HINT=""
