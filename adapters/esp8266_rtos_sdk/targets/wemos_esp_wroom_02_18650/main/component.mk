COMPONENT_SRCDIRS := .
COMPONENT_ADD_INCLUDEDIRS := . ../../../../../bsp/wemos_esp_wroom_02_18650
COMPONENT_REQUIRES := ev_platform
EV_WEMOS_BSP_DIR := ../../../../../bsp/wemos_esp_wroom_02_18650

# Developer-local WiFi credentials are intentionally kept out of git.
# If board_secrets.local.h exists, only this SDK target opts in to including it.
ifneq ($(wildcard $(EV_WEMOS_BSP_DIR)/board_secrets.local.h),)
CFLAGS += -DEV_BOARD_INCLUDE_LOCAL_SECRETS=1
endif

