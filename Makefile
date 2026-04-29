CC ?= cc
PYTHON ?= python3

CFLAGS ?= -std=c11 -Wall -Wextra -O0 -g0 -pedantic -DEV_HOST_BUILD \
    -Icore/include -Icore/generated/include -Iruntime/include -Imodules/include -Idrivers/include \
    -Iports/include -Iapps/demo/include -Iconfig
LDFLAGS ?=

BUILD_DIR := build/host
PROPERTY_BUILD_DIR := build/property
DOC_SITE_DIR := docs/generated/site

CORE_SRCS := \
    core/src/ev_version.c \
    core/src/ev_event_catalog.c \
    core/src/ev_actor_catalog.c \
    core/src/ev_msg.c \
    core/src/ev_route_table.c \
    core/src/ev_send.c \
    core/src/ev_publish.c \
    core/src/ev_dispose.c \
    core/src/ev_mailbox.c \
    core/src/ev_actor_runtime.c \
    core/src/ev_domain_pump.c \
    core/src/ev_system_pump.c \
    core/src/ev_lease_pool.c \
    core/src/ev_rtc_actor.c \
    core/src/ev_ds18b20_actor.c \
    core/src/ev_mcp23008_actor.c \
    core/src/ev_panel_actor.c \
    core/src/ev_oled_actor.c \
    core/src/ev_supervisor_actor.c \
    core/src/ev_power_actor.c \
    core/src/ev_watchdog_actor.c \
    core/src/ev_network_actor.c \
    core/src/ev_command_actor.c

RUNTIME_SRCS := \
    runtime/src/ev_actor_modules.c \
    runtime/src/ev_actor_instance.c \
    runtime/src/ev_active_route_table.c \
    runtime/src/ev_runtime_graph.c \
    runtime/src/ev_runtime_scheduler.c \
    runtime/src/ev_timer_service.c \
    runtime/src/ev_ingress_service.c \
    runtime/src/ev_quiescence_service.c \
    runtime/src/ev_delivery_service.c \
    runtime/src/ev_runtime_poll.c \
    runtime/src/ev_power_manager.c \
    runtime/src/ev_fault_bus.c \
    runtime/src/ev_metrics_registry.c \
    runtime/src/ev_trace_ring.c \
    runtime/src/ev_command_security.c \
    runtime/src/ev_network_outbox.c

MODULE_SRCS := \
    modules/src/ev_module_layer.c

DRIVER_SRCS := \
    drivers/src/ev_driver_layer.c

APP_SRCS := \
    apps/demo/ev_demo_app.c

TEST_SUPPORT_SRCS := \
    tests/host/fakes/fake_i2c_port.c \
    tests/host/fakes/fake_irq_port.c \
    tests/host/fakes/fake_onewire_port.c \
    tests/host/fakes/fake_system_port.c \
    tests/host/fakes/fake_log_port.c \
    tests/host/fakes/fake_wdt_port.c \
    tests/host/fakes/fake_net_port.c

COMMON_SRCS := $(CORE_SRCS) $(RUNTIME_SRCS) $(MODULE_SRCS) $(DRIVER_SRCS) $(APP_SRCS) $(TEST_SUPPORT_SRCS)
COMMON_OBJS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(COMMON_SRCS))

HOST_TESTS := \
    test_catalog \
    test_msg_contract \
    test_route_table \
    test_route_spans \
    test_dispatch_contract \
    test_mailbox_contract \
    test_actor_runtime \
    test_lease_pool_contract \
    test_runtime_diagnostics \
    test_actor_pump_contract \
    test_domain_pump_contract \
    test_system_pump_contract \
    test_power_actor_contract \
    test_watchdog_actor_contract \
    test_demo_app_watchdog_contract \
    test_demo_app_sleep_quiescence \
    test_irq_observability \
    test_bsp_runtime_profile \
    test_demo_app_contract \
    test_demo_app_fault_contract \
    test_app_starvation \
    test_app_fairness \
    test_network_isolation \
    test_command_actor_contract \
    test_runtime_actor_instance_descriptors \
    test_runtime_builder_route_validation \
    test_runtime_disabled_routes \
    test_runtime_graph_publish_send \
    test_runtime_graph_canonical_scheduler \
    test_runtime_builder_framework \
    test_timer_quiescence_framework \
    test_runtime_quiescence_time_aware \
    test_runtime_sequence_ingress \
    test_runtime_sequence_network_outbox \
    test_fault_metrics_trace_framework \
    test_delivery_command_network_framework

HOST_TEST_BINS := $(addprefix $(BUILD_DIR)/,$(HOST_TESTS))

PROPERTY_TESTS := \
    test_framework_property

PROPERTY_TEST_BINS := $(addprefix $(PROPERTY_BUILD_DIR)/,$(PROPERTY_TESTS))

.PHONY: all host-test property-test routegen routegen-check static-contracts memory-budget quality-gate docgen docs clean

all: host-test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PROPERTY_BUILD_DIR):
	mkdir -p $(PROPERTY_BUILD_DIR)

$(BUILD_DIR)/obj/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%: tests/host/%.c $(COMMON_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(COMMON_OBJS) $< $(LDFLAGS) -o $@

$(PROPERTY_BUILD_DIR)/%: tests/property/%.c $(COMMON_OBJS) | $(PROPERTY_BUILD_DIR)
	$(CC) $(CFLAGS) $(COMMON_OBJS) $< $(LDFLAGS) -o $@

host-test: routegen $(HOST_TEST_BINS)
	@set -e; for t in $(HOST_TEST_BINS); do ./$$t; done
	@echo "host tests passed"

property-test: routegen $(PROPERTY_TEST_BINS)
	@set -e; for t in $(PROPERTY_TEST_BINS); do ./$$t; done
	@echo "property tests passed"

routegen:
	$(PYTHON) tools/routegen/routegen.py

routegen-check: routegen
	$(PYTHON) tools/audit/routegen_check.py

static-contracts: routegen
	$(PYTHON) tools/audit/static_contracts.py

memory-budget: routegen
	$(PYTHON) tools/audit/memory_budget.py

.NOTPARALLEL: quality-gate
quality-gate: clean routegen-check static-contracts memory-budget host-test property-test
	@echo "quality-gate passed"

docgen: routegen
	$(PYTHON) tools/docgen/docgen.py

docs: docgen
	rm -f docs/generated/doxygen-warnings.log
	doxygen Doxyfile
	@if [ -s docs/generated/doxygen-warnings.log ]; then \
		echo "error: Doxygen warnings detected; see docs/generated/doxygen-warnings.log" >&2; \
		cat docs/generated/doxygen-warnings.log >&2; \
		exit 1; \
	fi

clean:
	rm -rf build docs/generated/*
	mkdir -p docs/generated
	touch docs/generated/.gitkeep
