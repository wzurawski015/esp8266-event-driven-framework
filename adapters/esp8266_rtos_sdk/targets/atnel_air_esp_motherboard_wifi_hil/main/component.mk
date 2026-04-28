COMPONENT_SRCDIRS := .
EV_BOARD_PROFILE_INCLUDE_DIR := ../../../../../bsp/atnel_air_esp_motherboard
EV_BOARD_PROFILE_LOCAL_DIR := ../../../../bsp/atnel_air_esp_motherboard
COMPONENT_ADD_INCLUDEDIRS := . $(EV_BOARD_PROFILE_INCLUDE_DIR)
COMPONENT_REQUIRES := ev_platform
CFLAGS += -DEV_HIL_WIFI_RECONNECT=1

# WiFi HIL needs private lab credentials, but they must remain untracked.
# Auto-enable the local secrets include only when the ignored file exists.
ifneq ($(firstword $(wildcard $(EV_BOARD_PROFILE_INCLUDE_DIR)/board_secrets.local.h) $(wildcard $(EV_BOARD_PROFILE_LOCAL_DIR)/board_secrets.local.h)),)
CFLAGS += -DEV_BOARD_INCLUDE_LOCAL_SECRETS=1
endif
