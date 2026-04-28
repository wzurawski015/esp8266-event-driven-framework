# Host-side USB-UART profile. Common CP210x VID/PID is used as a target hint;
# use target_usb_uart.local.profile or FW_ESPPORT when multiple adapters exist.
EV_TARGET_NAME="wemos_esp_wroom_02_18650"
EV_TARGET_USB_UART_BRIDGE="CP2102 / Silicon Labs CP210x USB-UART"
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
