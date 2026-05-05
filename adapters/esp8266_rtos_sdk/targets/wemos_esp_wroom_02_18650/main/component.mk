COMPONENT_SRCDIRS := .
EV_WEMOS_BSP_INCLUDE_DIR := ../../../../../bsp/wemos_esp_wroom_02_18650
COMPONENT_ADD_INCLUDEDIRS := . $(EV_WEMOS_BSP_INCLUDE_DIR)
COMPONENT_REQUIRES := ev_platform
EV_WEMOS_BSP_SECRETS_DIR := $(abspath $(COMPONENT_PATH)/$(EV_WEMOS_BSP_INCLUDE_DIR))
EV_WEMOS_BSP_PROJECT_SECRETS_DIR := $(abspath $(PROJECT_PATH)/../../../../bsp/wemos_esp_wroom_02_18650)

# Developer-local WiFi credentials are intentionally kept out of git.
# If board_secrets.local.h exists, only this SDK target opts in to including it.
# Use absolute paths for the existence check because ESP-IDF may parse
# component.mk from a generated build directory, not from the component source
# directory.  COMPONENT_ADD_INCLUDEDIRS remains source-relative as expected by
# the legacy ESP8266 RTOS SDK make backend.
ifneq ($(firstword $(wildcard $(EV_WEMOS_BSP_SECRETS_DIR)/board_secrets.local.h) $(wildcard $(EV_WEMOS_BSP_PROJECT_SECRETS_DIR)/board_secrets.local.h)),)
CFLAGS += -DEV_BOARD_INCLUDE_LOCAL_SECRETS=1
endif
