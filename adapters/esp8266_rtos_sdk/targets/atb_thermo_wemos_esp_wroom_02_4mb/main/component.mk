COMPONENT_SRCDIRS := .
EV_ATB_THERMO_BSP_INCLUDE_DIR := ../../../../../bsp/atb_thermo_wemos_esp_wroom_02_4mb
COMPONENT_ADD_INCLUDEDIRS := . $(EV_ATB_THERMO_BSP_INCLUDE_DIR)
COMPONENT_REQUIRES := ev_platform
EV_ATB_THERMO_BSP_SECRETS_DIR := $(abspath $(COMPONENT_PATH)/$(EV_ATB_THERMO_BSP_INCLUDE_DIR))
EV_ATB_THERMO_BSP_PROJECT_SECRETS_DIR := $(abspath $(PROJECT_PATH)/../../../../bsp/atb_thermo_wemos_esp_wroom_02_4mb)

ifneq ($(firstword $(wildcard $(EV_ATB_THERMO_BSP_SECRETS_DIR)/board_secrets.local.h) $(wildcard $(EV_ATB_THERMO_BSP_PROJECT_SECRETS_DIR)/board_secrets.local.h)),)
CFLAGS += -DEV_BOARD_INCLUDE_LOCAL_SECRETS=1
endif
