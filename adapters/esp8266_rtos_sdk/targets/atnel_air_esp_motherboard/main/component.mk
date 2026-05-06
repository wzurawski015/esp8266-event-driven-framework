COMPONENT_SRCDIRS := .
EV_BOARD_PROFILE_INCLUDE_DIR := ../../../../../bsp/atnel_air_esp_motherboard
EV_BOARD_PROFILE_SECRETS_DIR := $(abspath $(COMPONENT_PATH)/$(EV_BOARD_PROFILE_INCLUDE_DIR))
EV_BOARD_PROFILE_PROJECT_SECRETS_DIR := $(abspath $(PROJECT_PATH)/../../../../bsp/atnel_air_esp_motherboard)
COMPONENT_ADD_INCLUDEDIRS := . $(EV_BOARD_PROFILE_INCLUDE_DIR)
COMPONENT_REQUIRES := ev_platform

# Developer-local WiFi/MQTT credentials are intentionally kept out of git.
# When board_secrets.local.h is present, enable the opt-in include in
# board_profile.h without requiring fragile manual CFLAGS overrides.
# Use absolute paths for the existence check because ESP-IDF may parse
# component.mk from a generated build directory, not from the component source
# directory.  COMPONENT_ADD_INCLUDEDIRS remains source-relative as expected by
# the legacy ESP8266 RTOS SDK make backend.
ifneq ($(firstword $(wildcard $(EV_BOARD_PROFILE_SECRETS_DIR)/board_secrets.local.h) $(wildcard $(EV_BOARD_PROFILE_PROJECT_SECRETS_DIR)/board_secrets.local.h)),)
CFLAGS += -DEV_BOARD_INCLUDE_LOCAL_SECRETS=1
endif
