CC ?= cc
PYTHON ?= python3

CFLAGS ?= -std=c11 -Wall -Wextra -O0 -g0 -pedantic -DEV_HOST_BUILD \
    -Icore/include -Iactors/device/include -Iactors/framework/include -Icore/generated/include -Iruntime/include -Imodules/include -Idrivers/include \
    -Iports/include -Iapps/demo/include -Iconfig
BENCH_CFLAGS ?= $(filter-out -O0,$(CFLAGS)) -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

BUILD_DIR := build/host
PROPERTY_BUILD_DIR := build/property
BENCH_BUILD_DIR := build/bench
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
    core/src/ev_lease_pool.c

ACTOR_DEVICE_SRCS := \
    actors/device/ev_rtc_actor.c \
    actors/device/ev_ds18b20_actor.c \
    actors/device/ev_mcp23008_actor.c \
    actors/device/ev_panel_actor.c \
    actors/device/ev_oled_actor.c

ACTOR_FRAMEWORK_SRCS := \
    actors/framework/ev_supervisor_actor.c \
    actors/framework/ev_power_actor.c \
    actors/framework/ev_watchdog_actor.c \
    actors/framework/ev_network_actor.c \
    actors/framework/ev_command_actor.c

RUNTIME_SRCS := \
    runtime/src/ev_actor_modules.c \
    runtime/src/ev_actor_instance.c \
    runtime/src/ev_active_route_table.c \
    runtime/src/ev_runtime_graph.c \
    runtime/src/ev_runtime_scheduler.c \
    runtime/src/ev_actor_publish_port.c \
    runtime/src/ev_timer_service.c \
    runtime/src/ev_ingress_service.c \
    runtime/src/ev_quiescence_service.c \
    runtime/src/ev_delivery_service.c \
    runtime/src/ev_runtime_poll.c \
    runtime/src/ev_runtime_loop.c \
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
    apps/demo/ev_demo_app.c \
    apps/demo/ev_demo_runtime_instances.c

TEST_SUPPORT_SRCS := \
    tests/host/fakes/fake_clock_port.c \
    tests/host/fakes/fake_i2c_port.c \
    tests/host/fakes/fake_irq_port.c \
    tests/host/fakes/fake_onewire_port.c \
    tests/host/fakes/fake_system_port.c \
    tests/host/fakes/fake_log_port.c \
    tests/host/fakes/fake_wdt_port.c \
    tests/host/fakes/fake_net_port.c

COMMON_SRCS := $(CORE_SRCS) $(ACTOR_DEVICE_SRCS) $(ACTOR_FRAMEWORK_SRCS) $(RUNTIME_SRCS) $(MODULE_SRCS) $(DRIVER_SRCS) $(APP_SRCS) $(TEST_SUPPORT_SRCS)
COMMON_OBJS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(COMMON_SRCS))

HOST_TESTS := \
    test_catalog \
    test_msg_contract \
    test_route_table \
    test_route_spans \
    test_active_route_table_spans \
    test_route_qos_delivery_policy \
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
    test_demo_app_boot_sequence_golden \
    test_demo_app_disabled_route_golden \
    test_demo_app_tick_order_golden \
    test_demo_app_sleep_guard_golden \
    test_demo_app_fairness_golden \
    test_demo_app_contract \
    test_demo_app_fault_contract \
    test_app_starvation \
    test_app_fairness \
    test_network_isolation \
    test_command_actor_contract \
    test_actor_module_descriptor_consistency \
    test_runtime_mailbox_layout \
    test_runtime_actor_instance_descriptors \
    test_runtime_builder_route_validation \
    test_runtime_disabled_routes \
    test_runtime_graph_publish_send \
    test_runtime_graph_opaque_contract \
    test_actor_publish_port \
    test_runtime_graph_canonical_scheduler \
    test_runtime_loop \
    test_runtime_builder_framework \
    test_timer_quiescence_framework \
    test_runtime_quiescence_time_aware \
    test_runtime_sequence_ingress \
    test_runtime_sequence_network_outbox \
    test_wemos_runtime_profile \
    test_demo_runtime_instances \
    test_demo_migration_blockers \
    test_fault_metrics_trace_framework \
    test_delivery_trace_timestamp \
    test_actor_layering_contract \
    test_delivery_command_network_framework

HOST_TEST_BINS := $(addprefix $(BUILD_DIR)/,$(HOST_TESTS))

PROPERTY_TESTS := \
    test_framework_property

PROPERTY_TEST_BINS := $(addprefix $(PROPERTY_BUILD_DIR)/,$(PROPERTY_TESTS))

BENCH_SRCS := $(COMMON_SRCS)
BENCH_COMMON_OBJS := $(patsubst %.c,$(BENCH_BUILD_DIR)/obj/%.o,$(BENCH_SRCS))
BENCH_TESTS := \
    bench_runtime_publish \
    bench_runtime_poll
BENCH_BINS := $(addprefix $(BENCH_BUILD_DIR)/,$(BENCH_TESTS))
BENCH_RESULTS := $(BENCH_BUILD_DIR)/results.txt

.PHONY: all host-test property-test bench perf-gate routegen mailbox-layoutgen routegen-check mailbox-layoutgen-check static-contracts actor-module-consistency descriptor-contracts private-repo-secrets-policy release-evidence-contracts public-release-safety-gate memory-budget sdk-matrix-check sdk-memory-matrix sdk-memory-release-gate production-release-gate quality-gate release-gate docgen docs clean
.SECONDARY: $(COMMON_OBJS) $(BENCH_COMMON_OBJS)

all: host-test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PROPERTY_BUILD_DIR):
	mkdir -p $(PROPERTY_BUILD_DIR)

$(BENCH_BUILD_DIR):
	mkdir -p $(BENCH_BUILD_DIR)

$(BUILD_DIR)/obj/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%: tests/host/%.c $(COMMON_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(COMMON_OBJS) $< $(LDFLAGS) -o $@

$(PROPERTY_BUILD_DIR)/%: tests/property/%.c $(COMMON_OBJS) | $(PROPERTY_BUILD_DIR)
	$(CC) $(CFLAGS) $(COMMON_OBJS) $< $(LDFLAGS) -o $@

$(BENCH_BUILD_DIR)/obj/%.o: %.c | $(BENCH_BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(BENCH_CFLAGS) -c $< -o $@

$(BENCH_BUILD_DIR)/%: tests/bench/%.c $(BENCH_COMMON_OBJS) | $(BENCH_BUILD_DIR)
	$(CC) $(BENCH_CFLAGS) $(BENCH_COMMON_OBJS) $< $(LDFLAGS) -o $@

host-test: routegen $(HOST_TEST_BINS)
	@set -e; for t in $(HOST_TEST_BINS); do ./$$t; done
	@echo "host tests passed"

property-test: routegen $(PROPERTY_TEST_BINS)
	@set -e; for t in $(PROPERTY_TEST_BINS); do ./$$t; done
	@echo "property tests passed"

bench: routegen $(BENCH_BINS)
	@mkdir -p $(BENCH_BUILD_DIR)
	@: > $(BENCH_RESULTS)
	@set -e; for t in $(BENCH_BINS); do ./$$t | tee -a $(BENCH_RESULTS); done
	@$(PYTHON) tools/bench_report.py $(BENCH_RESULTS)
	@echo "bench passed"

perf-gate: bench
	@echo "perf-gate passed (report-only baseline)"

routegen:
	$(PYTHON) tools/routegen/routegen.py
	$(PYTHON) tools/routegen/mailbox_layoutgen.py

mailbox-layoutgen:
	$(PYTHON) tools/routegen/mailbox_layoutgen.py

mailbox-layoutgen-check:
	$(PYTHON) tools/routegen/mailbox_layoutgen.py --check

routegen-check: mailbox-layoutgen-check
	$(PYTHON) tools/audit/routegen_check.py

static-contracts: routegen
	$(PYTHON) tools/audit/static_contracts.py

actor-module-consistency: routegen
	$(PYTHON) tools/audit/actor_module_descriptor_consistency.py

descriptor-contracts: actor-module-consistency
	@echo "descriptor-contracts passed"

private-repo-secrets-policy:
	$(PYTHON) tools/audit/private_repo_secrets_policy.py

release-evidence-contracts:
	$(PYTHON) tools/audit/release_evidence_contracts.py

public-release-safety-gate:
	PUBLIC_RELEASE=1 $(PYTHON) tools/audit/private_repo_secrets_policy.py

memory-budget: routegen
	$(PYTHON) tools/audit/memory_budget.py

sdk-matrix-check:
	$(PYTHON) tools/audit/sdk_matrix_check.py
	$(PYTHON) tools/audit/sdk_target_defaults_check.py

sdk-memory-matrix:
	$(PYTHON) tools/sdk_memory_report.py --self-test
	$(PYTHON) tools/sdk_memory_matrix.py --self-test
	$(PYTHON) tools/sdk_memory_matrix.py

sdk-memory-release-gate:
	$(PYTHON) tools/sdk_memory_report.py --self-test
	$(PYTHON) tools/sdk_memory_matrix.py --self-test
	EV_SDK_MEMORY_REQUIRE_PASS=1 $(PYTHON) tools/sdk_memory_matrix.py

.NOTPARALLEL: quality-gate
quality-gate: clean routegen-check static-contracts actor-module-consistency descriptor-contracts private-repo-secrets-policy release-evidence-contracts memory-budget host-test property-test
	@echo "quality-gate passed"

release-gate: quality-gate docgen docs
	@echo "release-gate passed"

production-release-gate: release-gate sdk-matrix-check sdk-memory-matrix
	$(PYTHON) tools/release_report.py
	@echo "production-release-gate host/docs/matrix checks passed; HIL remains opt-in unless EV_REQUIRE_HIL=1 is handled by external runner"

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
