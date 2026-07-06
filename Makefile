CC ?= cc
PYTHON ?= python3

HOST_INCLUDE_FLAGS ?= \
    -Icore/include -Iactors/device/include -Iactors/framework/include -Icore/generated/include -Iruntime/include -Imodules/include -Idrivers/include \
    -Iports/include -Iapps/demo/include -Iconfig
HOST_BASE_DEFINES ?= -DEV_HOST_BUILD
CFLAGS ?= -std=c11 -Wall -Wextra -O0 -g0 -pedantic $(HOST_BASE_DEFINES) $(HOST_INCLUDE_FLAGS)
HOST_STRICT_CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -O0 -g0 $(HOST_BASE_DEFINES) $(HOST_INCLUDE_FLAGS)
HOST_SANITIZE_CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(HOST_BASE_DEFINES) $(HOST_INCLUDE_FLAGS)
HOST_SANITIZE_LDFLAGS ?= -fsanitize=address,undefined
HOST_TSAN_CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=thread $(HOST_BASE_DEFINES) $(HOST_INCLUDE_FLAGS)
HOST_TSAN_LDFLAGS ?= -fsanitize=thread
COVERAGE_CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -O0 -g --coverage $(HOST_BASE_DEFINES) $(HOST_INCLUDE_FLAGS)
COVERAGE_LDFLAGS ?= --coverage
CLANG_TIDY ?= clang-tidy
CLANG_TIDY_CHECKS ?= -*,clang-analyzer-*,bugprone-*,cert-*,performance-*,portability-*,-bugprone-easily-swappable-parameters
BENCH_CFLAGS ?= $(filter-out -O0,$(CFLAGS)) -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
DEPFLAGS ?= -MMD -MP

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
    actors/device/ev_bh1750_actor.c \
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
    runtime/src/ev_qos_contract.c \
    runtime/src/ev_runtime_poll.c \
    runtime/src/ev_runtime_loop.c \
    runtime/src/ev_power_manager.c \
    runtime/src/ev_power_state_machine.c \
    runtime/src/ev_fault_bus.c \
    runtime/src/ev_metrics_registry.c \
    runtime/src/ev_trace_ring.c \
    runtime/src/ev_command_security.c \
    runtime/src/ev_network_outbox.c

MODULE_SRCS := \
    modules/src/ev_module_layer.c

DRIVER_SRCS := \
    drivers/src/ev_driver_layer.c \
    drivers/src/ev_ds18b20_driver.c \
    drivers/src/ev_bh1750_driver.c

APP_SRCS := \
    apps/demo/ev_demo_app.c \
    apps/demo/ev_demo_board_wiring.c \
    apps/demo/ev_demo_policy.c \
    apps/demo/ev_demo_presentation.c \
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
    test_qos_contract_table \
    test_qos_route_module_compatibility \
    test_qos_mailbox_algorithms \
    test_route_registry_integration \
    test_dispatch_contract \
    test_mailbox_contract \
    test_actor_runtime \
    test_lease_pool_contract \
    test_zero_copy_payload_contract \
    test_i2c_port_contract \
    test_ds18b20_driver_contract \
    test_ds18b20_actor_deadline \
    test_bh1750_driver_contract \
    test_bh1750_actor_contract \
    test_runtime_diagnostics \
    test_actor_pump_contract \
    test_domain_pump_contract \
    test_system_pump_contract \
    test_power_actor_contract \
    test_power_state_machine \
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
    test_demo_composition_root_contract \
    test_deterministic_fuzz_contracts \
    test_delivery_command_network_framework

HOST_TEST_BINS := $(addprefix $(BUILD_DIR)/,$(HOST_TESTS))

COVERAGE_TESTS := \
    test_msg_contract \
    test_mailbox_contract \
    test_lease_pool_contract \
    test_zero_copy_payload_contract \
    test_power_state_machine \
    test_qos_contract_table \
    test_qos_mailbox_algorithms \
    test_deterministic_fuzz_contracts


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
PERF_BUDGETS ?= config/perf_budgets.json

.PHONY: all host-test property-test privacy-classification-self-test redaction-self-test evidence-redaction-check evidence-redaction-scrub repair-evidence-redaction patch-hygiene-gate secret-safe-working-tree-clean-gate-self-test secret-safe-working-tree-clean-gate working-tree-clean-gate board-wiring-truth-gate board-wiring-contract-gate no-direct-sdk-i2c-gate i2c-speed-policy-gate i2c-logic-analyzer-contract-self-test onewire-timing-contract-self-test hil-atb-thermo-parser-self-test architecture-layer-gate sdk-project-warning-self-test sdk-project-warning-gate sdk-project-warning-gate-real runtime-eventflow-budget-self-test runtime-eventflow-budget-gate hil-atnel-onewire-evidence hil-atnel-onewire-gate hil-atb-thermo-gate hil-logic-analyzer-readiness-gate ds18b20-driver-test bh1750-driver-test host-sanitize-ds18b20-test host-sanitize-bh1750-test host-strict-test host-sanitize-cc-check host-sanitize-core-test host-sanitize-runtime-test host-sanitize-actors-test host-sanitize-test host-sanitize-i2c-test host-sanitize-drivers-test host-sanitize-bh1750-actor-test host-tsan-cc-check host-tsan-test clang-tidy-gate host-gcc-analyzer-gate host-static-analysis-gate static-analysis-gate host-coverage-test coverage-report coverage-gate fuzz-smoke-gate fuzz-sanitize-gate ub-hardening-gate safety-gate i2c-sdk-bug-avoidance-gate hotpath-zero-alloc-gate bench perf-report perf-budget-gate perf-gate routegen mailbox-layoutgen routegen-check mailbox-layoutgen-check static-contracts actor-module-consistency descriptor-contracts route-registry-integration-gate private-repo-secrets-policy release-evidence-contracts release-report-consistency-gate qos-contracts public-release-safety-gate memory-budget sdk-matrix-check sdk-matrix-warning-policy-self-test sdk-build-workflow-contract-gate sdk-memory-matrix sdk-memory-release-gate sdk-build-evidence sdk-map-stack-evidence sdk-evidence-gate sdk-import-evidence sdk-import-evidence-gate sdk-import-flash-evidence sdk-flash-evidence-gate sdk-canonical-evidence-gate sdk-full-evidence-gate evidence-importer-hardening-gate hil-atnel-i2c-flash hil-atnel-i2c-monitor hil-atnel-i2c-evidence hil-atnel-i2c-gate hil-import-atnel-i2c-evidence hil-import-wemos-smoke-evidence hil-import-wemos-smoke-late-attach-evidence hil-import-wemos-deepsleep-evidence hil-import-all-evidence hil-wemos-smoke-late-attach-self-test hil-real-evidence-gate hil-wemos-smoke-flash hil-wemos-smoke-monitor hil-wemos-smoke-evidence hil-wemos-smoke-gate hil-wemos-deepsleep-wake-gate eventflow-hardware-evidence-report eventflow-hardware-evidence-gate eventflow-evidence-explain eventflow-release-gate operator-transcript-split-self-test operator-transcript-stage-evidence operator-transcript-evidence-gate operator-monitor-exit-classification-self-test wemos-one-shot-evidence-preflight wemos-one-shot-evidence-capture wemos-one-shot-evidence-gate wemos-one-shot-evidence-explain wemos-one-shot-evidence-report wemos-one-shot-evidence-self-test wemos-one-shot-sdk-import wemos-one-shot-sdk-import-gate wemos-one-shot-flash-import-gate wemos-one-shot-deepsleep-evidence-capture wemos-one-shot-deepsleep-evidence-gate wemos-one-shot-deepsleep-evidence-explain eventflow-one-shot-evidence-gate esp8266-target-timing-self-test esp8266-target-timing-report esp8266-target-timing-gate wemos-one-shot-target-timing-gate production-release-gate release-prearchive-gate release-archive release-archive-workflow-contract-gate release-archive-content-scope-gate release-archive-self-clean-gate runtime-soak-contract-self-test wemos-runtime-soak-gate sdk-memory-stack-regression-self-test sdk-memory-stack-regression-gate bh1750-actor-test quality-gate release-gate docgen docs clean
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
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%: tests/host/%.c $(COMMON_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(COMMON_OBJS) $< $(LDFLAGS) -o $@

$(PROPERTY_BUILD_DIR)/%: tests/property/%.c $(COMMON_OBJS) | $(PROPERTY_BUILD_DIR)
	$(CC) $(CFLAGS) $(COMMON_OBJS) $< $(LDFLAGS) -o $@

$(BENCH_BUILD_DIR)/obj/%.o: %.c | $(BENCH_BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(BENCH_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BENCH_BUILD_DIR)/%: tests/bench/%.c $(BENCH_COMMON_OBJS) | $(BENCH_BUILD_DIR)
	$(CC) $(BENCH_CFLAGS) $(BENCH_COMMON_OBJS) $< $(LDFLAGS) -o $@

# Header dependency files keep host/release gates from reusing stale objects after
# generated headers, board profiles, actor contracts, or test fakes change.
-include $(COMMON_OBJS:.o=.d)
-include $(BENCH_COMMON_OBJS:.o=.d)

host-test: routegen $(HOST_TEST_BINS)
	@set -e; for t in $(HOST_TEST_BINS); do ./$$t; done
	@echo "host tests passed"

property-test: routegen $(PROPERTY_TEST_BINS)
	@set -e; for t in $(PROPERTY_TEST_BINS); do ./$$t; done
	@echo "property tests passed"

host-strict-test:
	@echo "host-strict-test: C17 + -Werror host build"
	@$(MAKE) --no-print-directory BUILD_DIR=build/host-strict CFLAGS="$(HOST_STRICT_CFLAGS)" LDFLAGS="$(LDFLAGS)" host-test

host-sanitize-cc-check:
	@mkdir -p build/host-sanitize
	@printf '%s\n' 'int main(void){return 0;}' > build/host-sanitize/sanitize_check.c
	@if ! $(CC) $(HOST_SANITIZE_CFLAGS) build/host-sanitize/sanitize_check.c $(HOST_SANITIZE_LDFLAGS) -o build/host-sanitize/sanitize_check >/dev/null 2>&1; then \
		echo "host-sanitize-test ENVIRONMENT_BLOCKED: compiler does not support -fsanitize=address,undefined"; \
		exit 77; \
	fi

host-sanitize-core-test: host-sanitize-cc-check
	@echo "host-sanitize-core-test: core message contract under ASAN/UBSAN"
	@mkdir -p build/host-sanitize-core
	$(CC) $(HOST_SANITIZE_CFLAGS) core/src/ev_event_catalog.c core/src/ev_actor_catalog.c core/src/ev_msg.c core/src/ev_dispose.c tests/host/test_msg_contract.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-core/test_msg_contract
	./build/host-sanitize-core/test_msg_contract

host-sanitize-runtime-test: host-sanitize-cc-check
	@echo "host-sanitize-runtime-test: runtime state-machine contract under ASAN/UBSAN"
	@mkdir -p build/host-sanitize-runtime
	$(CC) $(HOST_SANITIZE_CFLAGS) runtime/src/ev_power_state_machine.c tests/host/test_power_state_machine.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-runtime/test_power_state_machine
	./build/host-sanitize-runtime/test_power_state_machine

host-sanitize-actors-test: host-sanitize-cc-check
	@echo "host-sanitize-actors-test: DS18B20 actor deadline contract under ASAN/UBSAN"
	@mkdir -p build/host-sanitize-actors
	$(CC) $(HOST_SANITIZE_CFLAGS) core/src/ev_event_catalog.c core/src/ev_actor_catalog.c core/src/ev_msg.c core/src/ev_dispose.c core/src/ev_publish.c core/src/ev_route_table.c drivers/src/ev_ds18b20_driver.c actors/device/ev_ds18b20_actor.c tests/host/fakes/fake_onewire_port.c tests/host/test_ds18b20_actor_deadline.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-actors/test_ds18b20_actor_deadline
	./build/host-sanitize-actors/test_ds18b20_actor_deadline

host-sanitize-test: host-sanitize-core-test host-sanitize-runtime-test host-sanitize-actors-test host-sanitize-i2c-test host-sanitize-drivers-test
	@echo "host-sanitize-test passed (deterministic ASAN/UBSAN shards)"

host-sanitize-i2c-test: host-sanitize-cc-check
	@echo "host-sanitize-i2c-test: targeted I2C contract under ASAN/UBSAN"
	@mkdir -p build/host-sanitize-i2c
	$(CC) $(HOST_SANITIZE_CFLAGS) tests/host/fakes/fake_i2c_port.c tests/host/test_i2c_port_contract.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-i2c/test_i2c_port_contract
	./build/host-sanitize-i2c/test_i2c_port_contract

host-sanitize-ds18b20-test: host-sanitize-cc-check
	@echo "host-sanitize-ds18b20-test: pure DS18B20 driver contract under ASAN/UBSAN"
	@mkdir -p build/host-sanitize-drivers
	$(CC) $(HOST_SANITIZE_CFLAGS) drivers/src/ev_ds18b20_driver.c tests/host/fakes/fake_onewire_port.c tests/host/test_ds18b20_driver_contract.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-drivers/test_ds18b20_driver_contract
	./build/host-sanitize-drivers/test_ds18b20_driver_contract

host-sanitize-bh1750-test: host-sanitize-cc-check
	@echo "host-sanitize-bh1750-test: BH1750 driver and actor contracts under ASAN/UBSAN"
	@mkdir -p build/host-sanitize-drivers
	$(CC) $(HOST_SANITIZE_CFLAGS) drivers/src/ev_bh1750_driver.c tests/host/fakes/fake_i2c_port.c tests/host/test_bh1750_driver_contract.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-drivers/test_bh1750_driver_contract
	./build/host-sanitize-drivers/test_bh1750_driver_contract
	$(CC) $(HOST_SANITIZE_CFLAGS) core/src/ev_event_catalog.c core/src/ev_actor_catalog.c core/src/ev_msg.c core/src/ev_dispose.c core/src/ev_publish.c core/src/ev_route_table.c drivers/src/ev_bh1750_driver.c actors/device/ev_bh1750_actor.c tests/host/fakes/fake_i2c_port.c tests/host/test_bh1750_actor_contract.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-drivers/test_bh1750_actor_contract
	./build/host-sanitize-drivers/test_bh1750_actor_contract

host-sanitize-bh1750-actor-test: host-sanitize-cc-check
	@echo "host-sanitize-bh1750-actor-test: BH1750 actor contract under ASAN/UBSAN"
	@mkdir -p build/host-sanitize-actors
	$(CC) $(HOST_SANITIZE_CFLAGS) core/src/ev_event_catalog.c core/src/ev_actor_catalog.c core/src/ev_msg.c core/src/ev_dispose.c core/src/ev_publish.c core/src/ev_route_table.c drivers/src/ev_bh1750_driver.c actors/device/ev_bh1750_actor.c tests/host/fakes/fake_i2c_port.c tests/host/test_bh1750_actor_contract.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-actors/test_bh1750_actor_contract
	./build/host-sanitize-actors/test_bh1750_actor_contract

host-sanitize-drivers-test: host-sanitize-cc-check host-sanitize-ds18b20-test host-sanitize-bh1750-test host-sanitize-bh1750-actor-test
	@echo "host-sanitize-drivers-test: driver contracts under ASAN/UBSAN"
	@mkdir -p build/host-sanitize-drivers
	$(CC) $(HOST_SANITIZE_CFLAGS) drivers/src/ev_driver_layer.c tests/host/test_driver_layer_contract.c $(HOST_SANITIZE_LDFLAGS) $(LDFLAGS) -o build/host-sanitize-drivers/test_driver_layer_contract
	./build/host-sanitize-drivers/test_driver_layer_contract
	@echo "host-sanitize-drivers-test passed"

host-tsan-cc-check:
	@mkdir -p build/host-tsan
	@printf '%s\n' 'int main(void){return 0;}' > build/host-tsan/tsan_check.c
	@if ! $(CC) $(HOST_TSAN_CFLAGS) build/host-tsan/tsan_check.c $(HOST_TSAN_LDFLAGS) -o build/host-tsan/tsan_check >/dev/null 2>&1; then \
		echo "host-tsan-test ENVIRONMENT_BLOCKED: compiler does not support -fsanitize=thread"; \
		exit 77; \
	fi

host-tsan-test: host-tsan-cc-check
	@echo "host-tsan-test: ThreadSanitizer host build"
	@$(MAKE) --no-print-directory BUILD_DIR=build/host-tsan CFLAGS="$(HOST_TSAN_CFLAGS)" LDFLAGS="$(HOST_TSAN_LDFLAGS) $(LDFLAGS)" host-test

clang-tidy-gate:
	@if ! command -v $(CLANG_TIDY) >/dev/null 2>&1; then \
		echo "clang-tidy-gate ENVIRONMENT_BLOCKED: $(CLANG_TIDY) not found"; \
	else \
		echo "clang-tidy-gate: portable layers and representative host tests"; \
		$(CLANG_TIDY) --quiet \
			$(CORE_SRCS) $(ACTOR_DEVICE_SRCS) $(ACTOR_FRAMEWORK_SRCS) $(RUNTIME_SRCS) $(MODULE_SRCS) $(DRIVER_SRCS) $(APP_SRCS) $(TEST_SUPPORT_SRCS) \
			tests/host/test_msg_contract.c tests/host/test_mailbox_contract.c tests/host/test_delivery_trace_timestamp.c tests/property/test_framework_property.c \
			-checks='$(CLANG_TIDY_CHECKS)' -- $(HOST_STRICT_CFLAGS); \
		echo "clang-tidy-gate passed"; \
	fi

host-gcc-analyzer-gate:
	$(PYTHON) tools/safety/static_analysis_gate.py --backend gcc-analyzer

host-static-analysis-gate: host-gcc-analyzer-gate
	@echo "host-static-analysis-gate passed"

static-analysis-gate:
	$(PYTHON) tools/safety/static_analysis_gate.py
	@echo "static-analysis-gate passed"

host-coverage-test:
	@echo "host-coverage-test: gcov critical host subset"
	@$(MAKE) --no-print-directory BUILD_DIR=build/host-coverage CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS) $(LDFLAGS)" routegen $(addprefix build/host-coverage/,$(COVERAGE_TESTS))
	@set -e; for t in $(addprefix build/host-coverage/,$(COVERAGE_TESTS)); do ./$$t; done
	@echo "host-coverage-test passed"

coverage-report: host-coverage-test
	$(PYTHON) tools/safety/coverage_gate.py --report-only

coverage-gate: host-coverage-test
	$(PYTHON) tools/safety/coverage_gate.py

fuzz-smoke-gate: routegen $(BUILD_DIR)/test_deterministic_fuzz_contracts
	./$(BUILD_DIR)/test_deterministic_fuzz_contracts
	@echo "fuzz-smoke-gate passed"

fuzz-sanitize-gate: host-sanitize-cc-check
	@$(MAKE) --no-print-directory BUILD_DIR=build/fuzz-sanitize CFLAGS="$(HOST_SANITIZE_CFLAGS)" LDFLAGS="$(HOST_SANITIZE_LDFLAGS) $(LDFLAGS)" routegen build/fuzz-sanitize/test_deterministic_fuzz_contracts
	./build/fuzz-sanitize/test_deterministic_fuzz_contracts
	@echo "fuzz-sanitize-gate passed"

ub-hardening-gate: host-strict-test host-sanitize-test host-tsan-test fuzz-smoke-gate
	@set -e; \
	if $(MAKE) --no-print-directory static-analysis-gate; then :; else rc=$$?; if [ "$$rc" = "77" ] && [ "$${EV_UB_HARDENING_ALLOW_BLOCKED:-1}" = "1" ]; then echo "ub-hardening-gate static-analysis ENVIRONMENT_BLOCKED_ALLOWED"; else exit $$rc; fi; fi; \
	if $(MAKE) --no-print-directory coverage-gate; then :; else rc=$$?; if [ "$$rc" = "77" ] && [ "$${EV_UB_HARDENING_ALLOW_BLOCKED:-1}" = "1" ]; then echo "ub-hardening-gate coverage ENVIRONMENT_BLOCKED_ALLOWED"; else exit $$rc; fi; fi; \
	if $(MAKE) --no-print-directory fuzz-sanitize-gate; then :; else rc=$$?; if [ "$$rc" = "77" ] && [ "$${EV_UB_HARDENING_ALLOW_BLOCKED:-1}" = "1" ]; then echo "ub-hardening-gate fuzz-sanitize ENVIRONMENT_BLOCKED_ALLOWED"; else exit $$rc; fi; fi
	@echo "ub-hardening-gate passed"

safety-gate: host-strict-test host-sanitize-test
	@echo "safety-gate passed"


i2c-sdk-bug-avoidance-gate:
	$(PYTHON) tools/audit/i2c_sdk_bug_avoidance_check.py --self-test
	$(PYTHON) tools/audit/i2c_sdk_bug_avoidance_check.py

no-direct-sdk-i2c-gate: i2c-sdk-bug-avoidance-gate
	@echo "no-direct-sdk-i2c-gate passed"

board-wiring-truth-gate:
	$(PYTHON) tools/audit/board_wiring_truth.py --self-test
	$(PYTHON) tools/audit/board_wiring_truth.py

# Compatibility alias retained for Phase 4/5 operator scripts.
board-wiring-contract-gate: board-wiring-truth-gate
	@echo "board-wiring-contract-gate passed via board-wiring-truth-gate"

i2c-speed-policy-gate:
	$(PYTHON) tools/audit/i2c_speed_policy.py --self-test
	$(PYTHON) tools/audit/i2c_speed_policy.py

i2c-logic-analyzer-contract-self-test:
	$(PYTHON) tools/audit/hil_contracts.py --i2c-logic-analyzer-self-test

onewire-timing-contract-self-test:
	$(PYTHON) tools/audit/hil_contracts.py --onewire-timing-self-test

hotpath-zero-alloc-gate: routegen $(BUILD_DIR)/test_zero_copy_payload_contract
	$(PYTHON) tools/audit/hotpath_zero_alloc_contract.py
	./$(BUILD_DIR)/test_zero_copy_payload_contract
	@echo "hotpath-zero-alloc-gate passed"

bench: routegen $(BENCH_BINS)
	@mkdir -p $(BENCH_BUILD_DIR)
	@: > $(BENCH_RESULTS)
	@set -e; for t in $(BENCH_BINS); do ./$$t | tee -a $(BENCH_RESULTS); done
	@echo "bench passed"

perf-report: bench
	@$(PYTHON) tools/bench_report.py --report-only --budget $(PERF_BUDGETS) $(BENCH_RESULTS)
	@echo "perf-report passed (report-only)"

perf-budget-gate: bench
	@$(PYTHON) tools/bench_report.py --strict --budget $(PERF_BUDGETS) $(BENCH_RESULTS)
	@echo "perf-budget-gate passed"

perf-gate: perf-budget-gate
	@echo "perf-gate passed (hard regression budgets)"

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

route-registry-integration-gate: $(BUILD_DIR)/test_route_registry_integration
	./$(BUILD_DIR)/test_route_registry_integration
	@echo "route-registry-integration-gate passed"

# Compatibility aliases for pure driver contracts.
ds18b20-driver-test: $(BUILD_DIR)/test_ds18b20_driver_contract
	./$(BUILD_DIR)/test_ds18b20_driver_contract

bh1750-driver-test: $(BUILD_DIR)/test_bh1750_driver_contract
	./$(BUILD_DIR)/test_bh1750_driver_contract

bh1750-actor-test: $(BUILD_DIR)/test_bh1750_actor_contract
	./$(BUILD_DIR)/test_bh1750_actor_contract

privacy-classification-self-test:
	$(PYTHON) tools/lib/ev_privacy_classification.py --self-test

private-repo-secrets-policy:
	$(PYTHON) tools/audit/private_repo_secrets_policy.py --mode "$${EV_REPO_MODE:-PRIVATE_REPO}"

release-evidence-contracts:
	$(PYTHON) tools/audit/release_evidence_contracts.py
	$(PYTHON) tools/audit/release_report_consistency.py

release-report-consistency-gate:
	$(PYTHON) tools/audit/release_report_consistency.py --self-test
	$(PYTHON) tools/audit/release_report_consistency.py

qos-contracts:
	$(PYTHON) tools/audit/qos_contract_check.py

architecture-layer-gate:
	$(PYTHON) tools/audit/layer_boundary_policy.py --self-test
	$(PYTHON) tools/audit/layer_boundary_policy.py

sdk-project-warning-self-test:
	$(PYTHON) tools/audit/sdk_warning_policy.py --self-test

sdk-project-warning-gate: sdk-project-warning-self-test
	@if [ -n "$${EV_SDK_BUILD_LOG:-}" ]; then \
		$(PYTHON) tools/audit/sdk_warning_policy.py --project-only --latest-build-session --session-kind build --strict-build-session "$${EV_SDK_BUILD_LOG}"; \
	else \
		echo "sdk-project-warning-gate ENVIRONMENT_BLOCKED: EV_SDK_BUILD_LOG not set; SDK jobs must pass a fresh build log"; \
		exit 77; \
	fi

sdk-project-warning-gate-real: sdk-project-warning-gate
	@echo "sdk-project-warning-gate-real passed"

runtime-eventflow-budget-self-test:
	$(PYTHON) tools/perf/parse_eventflow_runtime_metrics.py --self-test

runtime-eventflow-budget-gate: runtime-eventflow-budget-self-test $(BUILD_DIR)/test_runtime_eventflow_metrics_marker
	@mkdir -p build
	./$(BUILD_DIR)/test_runtime_eventflow_metrics_marker | tee build/runtime-eventflow-metrics.log
	$(PYTHON) tools/perf/parse_eventflow_runtime_metrics.py --log build/runtime-eventflow-metrics.log --json build/runtime-eventflow-metrics.json --require-real-sample

public-release-safety-gate:
	$(PYTHON) tools/audit/private_repo_secrets_policy.py --mode PUBLIC_RELEASE

memory-budget: routegen
	$(PYTHON) tools/audit/memory_budget.py

sdk-matrix-check:
	$(PYTHON) tools/audit/sdk_matrix_check.py
	$(PYTHON) tools/audit/sdk_target_defaults_check.py

sdk-matrix-warning-policy-self-test:
	$(PYTHON) tools/sdk_matrix.py self-test
	$(PYTHON) tools/audit/sdk_warning_policy.py --self-test

sdk-build-workflow-contract-gate:
	$(PYTHON) tools/audit/sdk_build_workflow_contract.py --self-test
	$(PYTHON) tools/audit/sdk_build_workflow_contract.py

sdk-memory-matrix:
	$(PYTHON) tools/sdk_memory_report.py --self-test
	$(PYTHON) tools/sdk_memory_matrix.py --self-test
	$(PYTHON) tools/sdk_memory_matrix.py

sdk-build-evidence:
	$(PYTHON) tools/release/capture_sdk_evidence.py --self-test
	$(PYTHON) tools/release/capture_sdk_evidence.py --capture

sdk-map-stack-evidence:
	$(PYTHON) tools/release/capture_sdk_evidence.py --summarize

sdk-evidence-gate:
	$(PYTHON) tools/release/capture_sdk_evidence.py --gate

sdk-import-evidence:
	$(PYTHON) tools/release/import_sdk_evidence.py --self-test
	$(PYTHON) tools/release/import_sdk_evidence.py

sdk-import-evidence-gate:
	$(PYTHON) tools/release/import_sdk_evidence.py --gate

sdk-import-flash-evidence:
	$(PYTHON) tools/release/parse_esptool_flash_log.py --self-test
	@if [ -n "$${EV_SDK_FLASH_EVIDENCE_IMPORT_ROOT:-}" ]; then \
		$(PYTHON) tools/release/parse_esptool_flash_log.py --import-root "$${EV_SDK_FLASH_EVIDENCE_IMPORT_ROOT}"; \
	elif [ -n "$${EV_SDK_FLASH_LOG:-}" ] && [ -n "$${EV_SDK_FLASH_TARGET:-}" ]; then \
		$(PYTHON) tools/release/parse_esptool_flash_log.py --target "$${EV_SDK_FLASH_TARGET}" --log "$${EV_SDK_FLASH_LOG}"; \
	else \
		echo "EV_SDK_FLASH_EVIDENCE ENVIRONMENT_BLOCKED: set EV_SDK_FLASH_EVIDENCE_IMPORT_ROOT or EV_SDK_FLASH_LOG+EV_SDK_FLASH_TARGET"; exit 77; \
	fi

sdk-flash-evidence-gate:
	$(PYTHON) tools/release/parse_esptool_flash_log.py --gate

sdk-canonical-evidence-gate:
	$(PYTHON) tools/release/capture_sdk_evidence.py --self-test
	$(PYTHON) tools/release/import_sdk_evidence.py --self-test
	$(PYTHON) tools/release/parse_esptool_flash_log.py --self-test
	$(PYTHON) tools/audit/release_evidence_contracts.py
	$(PYTHON) tools/audit/static_contracts.py
	@echo "sdk-canonical-evidence-gate passed"

redaction-self-test: privacy-classification-self-test
	$(PYTHON) tools/lib/ev_redaction.py --self-test
	$(PYTHON) tools/release/redact_existing_evidence.py --self-test

evidence-redaction-check:
	$(PYTHON) tools/release/redact_existing_evidence.py --mode "$${EV_REPO_MODE:-PRIVATE_REPO}"

evidence-redaction-scrub:
	$(PYTHON) tools/release/redact_existing_evidence.py --mode "$${EV_REPO_MODE:-PRIVATE_REPO}" --apply

repair-evidence-redaction: evidence-redaction-scrub
	@echo "repair-evidence-redaction completed; review and commit sanitized evidence before running release gates"

patch-hygiene-gate:
	$(PYTHON) tools/audit/patch_hygiene.py --self-test
	$(PYTHON) tools/audit/patch_hygiene.py

secret-safe-working-tree-clean-gate-self-test:
	$(PYTHON) tools/audit/secret_safe_worktree.py --self-test

secret-safe-working-tree-clean-gate:
	$(PYTHON) tools/audit/secret_safe_worktree.py

working-tree-clean-gate: secret-safe-working-tree-clean-gate
	@echo "working-tree-clean-gate passed (secret-safe)"

evidence-importer-hardening-gate: redaction-self-test
	$(PYTHON) tools/release/capture_sdk_evidence.py --self-test
	$(PYTHON) tools/release/import_sdk_evidence.py --self-test
	$(PYTHON) tools/release/parse_esptool_flash_log.py --self-test
	$(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --self-test
	$(PYTHON) tools/hil/parse_wemos_smoke_log.py --self-test
	$(PYTHON) tools/hil/import_hil_serial_evidence.py --self-test
	$(PYTHON) tools/hil/eventflow_evidence_gate.py --self-test
	$(PYTHON) tools/audit/release_evidence_contracts.py
	$(PYTHON) tools/audit/static_contracts.py
	@echo "evidence-importer-hardening-gate passed"

sdk-full-evidence-gate: sdk-import-evidence-gate sdk-evidence-gate sdk-memory-release-gate
	@echo "sdk-full-evidence-gate passed"

hil-atnel-i2c-flash:
	@if [ "$${EV_HIL_ALLOW_FLASH:-}" = "1" ]; then ./tools/fw hil-i2c-flash; else echo "hil-atnel-i2c-flash ENVIRONMENT_BLOCKED: set EV_HIL_ALLOW_FLASH=1 and attach ATNEL I2C fixture"; exit 77; fi

hil-atnel-i2c-monitor:
	@if [ "$${EV_HIL_ALLOW_MONITOR:-}" = "1" ]; then ./tools/fw hil-i2c-monitor; else echo "hil-atnel-i2c-monitor ENVIRONMENT_BLOCKED: set EV_HIL_ALLOW_MONITOR=1 and attach ATNEL I2C fixture"; exit 77; fi

hil-atnel-i2c-evidence:
	$(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --self-test
	@if [ -n "$${EV_HIL_ATNEL_I2C_SERIAL_LOG:-}" ]; then \
		$(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --log "$${EV_HIL_ATNEL_I2C_SERIAL_LOG}"; \
	else \
		$(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --environment-blocked || true; \
	fi

hil-atnel-i2c-gate:
	@if [ -n "$${EV_HIL_ATNEL_I2C_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --check-only --log "$${EV_HIL_ATNEL_I2C_SERIAL_LOG}"; else $(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --check-only --environment-blocked; fi

hil-atnel-onewire-evidence:
	$(PYTHON) tools/hil/parse_atnel_onewire_hil_log.py --self-test
	@if [ -n "$${EV_HIL_ATNEL_ONEWIRE_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_atnel_onewire_hil_log.py --log "$${EV_HIL_ATNEL_ONEWIRE_SERIAL_LOG}"; else $(PYTHON) tools/hil/parse_atnel_onewire_hil_log.py --environment-blocked || true; fi

hil-atnel-onewire-gate:
	@if [ -n "$${EV_HIL_ATNEL_ONEWIRE_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_atnel_onewire_hil_log.py --check-only --log "$${EV_HIL_ATNEL_ONEWIRE_SERIAL_LOG}"; else $(PYTHON) tools/hil/parse_atnel_onewire_hil_log.py --check-only --environment-blocked; fi


hil-atb-thermo-parser-self-test:
	python3 tools/hil/parse_atb_thermo_hil_log.py --self-test

hil-atb-thermo-gate:
	@if [ -n "$${EV_ATB_THERMO_HIL_LOG:-}" ]; then \
		python3 tools/hil/parse_atb_thermo_hil_log.py "$${EV_ATB_THERMO_HIL_LOG}"; \
	else \
		python3 tools/hil/parse_atb_thermo_hil_log.py; \
	fi

hil-logic-analyzer-readiness-gate:
	$(PYTHON) tools/hil/logic_analyzer_readiness.py --self-test
	@if [ -n "$${EV_HIL_LOGIC_ANALYZER_MANIFEST:-}" ]; then $(PYTHON) tools/hil/logic_analyzer_readiness.py --manifest "$${EV_HIL_LOGIC_ANALYZER_MANIFEST}"; else $(PYTHON) tools/hil/logic_analyzer_readiness.py; fi

hil-wemos-smoke-flash:
	@if [ "$${EV_HIL_ALLOW_FLASH:-}" = "1" ]; then ./tools/fw wemos-smoke-flash; else echo "hil-wemos-smoke-flash ENVIRONMENT_BLOCKED: set EV_HIL_ALLOW_FLASH=1 and attach Wemos target"; exit 77; fi

hil-wemos-smoke-monitor:
	@if [ "$${EV_HIL_ALLOW_MONITOR:-}" = "1" ]; then ./tools/fw wemos-smoke-monitor; else echo "hil-wemos-smoke-monitor ENVIRONMENT_BLOCKED: set EV_HIL_ALLOW_MONITOR=1 and attach Wemos target"; exit 77; fi

hil-wemos-smoke-evidence:
	$(PYTHON) tools/hil/parse_wemos_smoke_log.py --self-test
	@if [ -n "$${EV_HIL_WEMOS_SMOKE_SERIAL_LOG:-}" ]; then \
		$(PYTHON) tools/hil/parse_wemos_smoke_log.py --log "$${EV_HIL_WEMOS_SMOKE_SERIAL_LOG}"; \
	else \
		$(PYTHON) tools/hil/parse_wemos_smoke_log.py --environment-blocked || true; \
	fi

hil-wemos-smoke-gate:
	@if [ -n "$${EV_HIL_WEMOS_SMOKE_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_wemos_smoke_log.py --log "$${EV_HIL_WEMOS_SMOKE_SERIAL_LOG}"; else $(PYTHON) tools/hil/parse_wemos_smoke_log.py --environment-blocked; fi

hil-wemos-deepsleep-wake-gate:
	@if [ -n "$${EV_HIL_WEMOS_DEEPSLEEP_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_wemos_smoke_log.py --deepsleep --log "$${EV_HIL_WEMOS_DEEPSLEEP_SERIAL_LOG}"; else $(PYTHON) tools/hil/parse_wemos_smoke_log.py --deepsleep --environment-blocked; fi

hil-import-atnel-i2c-evidence:
	$(PYTHON) tools/hil/import_hil_serial_evidence.py --self-test
	@if [ -n "$${EV_HIL_ATNEL_I2C_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --log "$${EV_HIL_ATNEL_I2C_SERIAL_LOG}"; else echo "hil-import-atnel-i2c-evidence ENVIRONMENT_BLOCKED: EV_HIL_ATNEL_I2C_SERIAL_LOG not set"; exit 77; fi

hil-import-wemos-smoke-evidence:
	$(PYTHON) tools/hil/import_hil_serial_evidence.py --self-test
	@if [ -n "$${EV_HIL_WEMOS_SMOKE_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_wemos_smoke_log.py --log "$${EV_HIL_WEMOS_SMOKE_SERIAL_LOG}"; else echo "hil-import-wemos-smoke-evidence ENVIRONMENT_BLOCKED: EV_HIL_WEMOS_SMOKE_SERIAL_LOG not set"; exit 77; fi

hil-wemos-smoke-late-attach-self-test:
	$(PYTHON) tools/hil/parse_wemos_smoke_log.py --self-test
	$(PYTHON) tools/hil/import_hil_serial_evidence.py --self-test

hil-import-wemos-smoke-late-attach-evidence:
	$(PYTHON) tools/hil/import_hil_serial_evidence.py --self-test
	@if [ -n "$${EV_HIL_WEMOS_SMOKE_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_wemos_smoke_log.py --allow-runtime-alive-fallback --normalize --log "$${EV_HIL_WEMOS_SMOKE_SERIAL_LOG}"; else echo "hil-import-wemos-smoke-late-attach-evidence ENVIRONMENT_BLOCKED: EV_HIL_WEMOS_SMOKE_SERIAL_LOG not set"; exit 77; fi

hil-import-wemos-deepsleep-evidence:
	$(PYTHON) tools/hil/import_hil_serial_evidence.py --self-test
	@if [ -n "$${EV_HIL_WEMOS_DEEPSLEEP_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_wemos_smoke_log.py --deepsleep --log "$${EV_HIL_WEMOS_DEEPSLEEP_SERIAL_LOG}"; else echo "hil-import-wemos-deepsleep-evidence ENVIRONMENT_BLOCKED: EV_HIL_WEMOS_DEEPSLEEP_SERIAL_LOG not set"; exit 77; fi

hil-import-all-evidence:
	$(PYTHON) tools/hil/import_hil_serial_evidence.py --import-all

hil-real-evidence-gate:
	$(PYTHON) tools/hil/import_hil_serial_evidence.py --gate

eventflow-hardware-evidence-report:
	$(PYTHON) tools/hil/eventflow_evidence_gate.py --self-test
	$(PYTHON) tools/hil/eventflow_evidence_gate.py --report

eventflow-hardware-evidence-gate:
	$(PYTHON) tools/hil/eventflow_evidence_gate.py --gate

eventflow-evidence-explain:
	$(PYTHON) tools/hil/eventflow_evidence_gate.py --explain

eventflow-release-gate: sdk-full-evidence-gate hil-real-evidence-gate eventflow-hardware-evidence-gate
	@echo "eventflow-release-gate passed"

operator-transcript-split-self-test:
	$(PYTHON) tools/release/split_operator_transcript.py --self-test

operator-transcript-stage-evidence:
	@if [ -n "$${EV_OPERATOR_TRANSCRIPT_LOG:-}" ]; then \
		$(PYTHON) tools/release/split_operator_transcript.py --input "$${EV_OPERATOR_TRANSCRIPT_LOG}" --target "$${EV_OPERATOR_TRANSCRIPT_TARGET:-wemos_esp_wroom_02_18650}" --output-dir "$${EV_OPERATOR_TRANSCRIPT_OUTPUT_DIR:-docs/release/operator_transcript_evidence/$${EV_OPERATOR_TRANSCRIPT_TARGET:-wemos_esp_wroom_02_18650}/current}" --run-parsers; \
	else \
		echo "operator-transcript-stage-evidence ENVIRONMENT_BLOCKED: EV_OPERATOR_TRANSCRIPT_LOG not set"; exit 77; \
	fi

operator-transcript-evidence-gate:
	$(PYTHON) tools/release/split_operator_transcript.py --gate

operator-monitor-exit-classification-self-test:
	$(PYTHON) tools/release/operator_exit_footer.py
	$(PYTHON) tools/release/split_operator_transcript.py --self-test
	$(PYTHON) tools/hil/parse_wemos_smoke_log.py --self-test


wemos-one-shot-evidence-preflight:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --preflight --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}"

wemos-one-shot-evidence-capture:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --capture --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --output-dir "$${EV_WEMOS_ONE_SHOT_OUTPUT_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current}"

wemos-one-shot-evidence-gate:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --gate --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --evidence-dir "$${EV_WEMOS_ONE_SHOT_OUTPUT_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current}"

wemos-one-shot-evidence-explain:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --explain --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --evidence-dir "$${EV_WEMOS_ONE_SHOT_OUTPUT_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current}"

wemos-one-shot-evidence-report:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --report --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --evidence-dir "$${EV_WEMOS_ONE_SHOT_OUTPUT_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current}"

wemos-one-shot-evidence-self-test:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --self-test


wemos-one-shot-sdk-import:
	$(PYTHON) tools/release/import_sdk_evidence.py --import-target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --from-one-shot-dir "$${EV_WEMOS_ONE_SHOT_EVIDENCE_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current}"

wemos-one-shot-sdk-import-gate:
	$(PYTHON) tools/release/import_sdk_evidence.py --gate

wemos-one-shot-flash-import-gate:
	$(PYTHON) tools/release/parse_esptool_flash_log.py --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --from-one-shot-dir "$${EV_WEMOS_ONE_SHOT_EVIDENCE_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current}"


wemos-one-shot-deepsleep-evidence-capture:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --capture-deepsleep --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --output-dir "$${EV_WEMOS_ONE_SHOT_DEEPSLEEP_OUTPUT_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current-deepsleep}"

wemos-one-shot-deepsleep-evidence-gate:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --gate --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --evidence-dir "$${EV_WEMOS_ONE_SHOT_DEEPSLEEP_OUTPUT_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current-deepsleep}"

wemos-one-shot-deepsleep-evidence-explain:
	$(PYTHON) tools/release/wemos_one_shot_evidence.py --explain --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}" --evidence-dir "$${EV_WEMOS_ONE_SHOT_DEEPSLEEP_OUTPUT_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current-deepsleep}"


eventflow-one-shot-evidence-gate:
	$(PYTHON) tools/hil/eventflow_evidence_gate.py --gate --one-shot-required --one-shot-dir "$${EV_WEMOS_ONE_SHOT_EVIDENCE_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current}"


esp8266-target-timing-self-test:
	$(PYTHON) tools/perf/parse_esp8266_target_timing.py --self-test

esp8266-target-timing-report:
	@if [ -n "$${EV_ESP8266_TARGET_TIMING_SERIAL_LOG:-}" ]; then \
		$(PYTHON) tools/perf/parse_esp8266_target_timing.py --serial-log "$${EV_ESP8266_TARGET_TIMING_SERIAL_LOG}" --target "$${EV_ESP8266_TARGET_TIMING_TARGET:-wemos_esp_wroom_02_18650}" --output-dir "$${EV_ESP8266_TARGET_TIMING_OUTPUT_DIR:-docs/release/target_timing/$${EV_ESP8266_TARGET_TIMING_TARGET:-wemos_esp_wroom_02_18650}/current}"; \
	else \
		echo "esp8266-target-timing-report ENVIRONMENT_BLOCKED: EV_ESP8266_TARGET_TIMING_SERIAL_LOG not set"; exit 77; \
	fi

esp8266-target-timing-gate:
	@if [ -n "$${EV_ESP8266_TARGET_TIMING_SERIAL_LOG:-}" ]; then \
		$(PYTHON) tools/perf/parse_esp8266_target_timing.py --serial-log "$${EV_ESP8266_TARGET_TIMING_SERIAL_LOG}" --target "$${EV_ESP8266_TARGET_TIMING_TARGET:-wemos_esp_wroom_02_18650}" --output-dir "$${EV_ESP8266_TARGET_TIMING_OUTPUT_DIR:-docs/release/target_timing/$${EV_ESP8266_TARGET_TIMING_TARGET:-wemos_esp_wroom_02_18650}/current}"; \
	else \
		echo "esp8266-target-timing-gate ENVIRONMENT_BLOCKED: EV_ESP8266_TARGET_TIMING_SERIAL_LOG not set"; exit 77; \
	fi

wemos-one-shot-target-timing-gate:
	$(PYTHON) tools/perf/parse_esp8266_target_timing.py --from-one-shot-dir "$${EV_WEMOS_ONE_SHOT_EVIDENCE_DIR:-docs/release/wemos_one_shot_evidence/$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}/current}" --target "$${EV_WEMOS_ONE_SHOT_TARGET:-wemos_esp_wroom_02_18650}"
	$(PYTHON) tools/hil/eventflow_evidence_gate.py --self-test


sdk-memory-release-gate:
	$(PYTHON) tools/sdk_memory_report.py --self-test
	$(PYTHON) tools/sdk_memory_matrix.py --self-test
	EV_SDK_MEMORY_REQUIRE_PASS=1 $(PYTHON) tools/sdk_memory_matrix.py

release-archive-workflow-contract-gate:
	$(PYTHON) tools/audit/release_archive_workflow_contract.py --self-test
	$(PYTHON) tools/audit/release_archive_workflow_contract.py

release-archive-content-scope-gate:
	$(PYTHON) tools/audit/release_archive_content_scope.py --self-test
	$(PYTHON) tools/audit/release_archive_content_scope.py

release-archive-self-clean-gate:
	$(PYTHON) tools/release/verify_archive_self_clean.py --self-test
	@if [ -n "$${EV_RELEASE_ARCHIVE:-}" ]; then \
		$(PYTHON) tools/release/verify_archive_self_clean.py --mode "$${EV_REPO_MODE:-PRIVATE_REPO}" "$${EV_RELEASE_ARCHIVE}"; \
	elif [ -n "$${EV_RELEASE_ARCHIVE_PATH:-}" ]; then \
		$(PYTHON) tools/release/verify_archive_self_clean.py --mode "$${EV_REPO_MODE:-PRIVATE_REPO}" "$${EV_RELEASE_ARCHIVE_PATH}"; \
	else \
		echo "release-archive-self-clean-gate SELF_TEST_PASS: set EV_RELEASE_ARCHIVE_PATH to verify a real archive"; \
	fi

runtime-soak-contract-self-test:
	$(PYTHON) tools/perf/parse_runtime_soak_log.py --self-test

wemos-runtime-soak-gate: runtime-soak-contract-self-test
	@if [ -n "$${EV_RUNTIME_SOAK_LOG:-}" ]; then $(PYTHON) tools/perf/parse_runtime_soak_log.py --log "$${EV_RUNTIME_SOAK_LOG}"; else echo "wemos-runtime-soak-gate ENVIRONMENT_BLOCKED: EV_RUNTIME_SOAK_LOG not set; provide WiFi-on runtime soak transcript"; exit 77; fi

sdk-memory-stack-regression-self-test:
	$(PYTHON) tools/audit/sdk_memory_stack_regression.py --self-test

sdk-memory-stack-regression-gate: sdk-memory-stack-regression-self-test
	$(PYTHON) tools/audit/sdk_memory_stack_regression.py

.NOTPARALLEL: quality-gate
quality-gate: clean redaction-self-test evidence-redaction-check routegen-check static-contracts board-wiring-truth-gate board-wiring-contract-gate i2c-speed-policy-gate i2c-logic-analyzer-contract-self-test onewire-timing-contract-self-test hil-atb-thermo-parser-self-test architecture-layer-gate sdk-project-warning-self-test sdk-build-workflow-contract-gate patch-hygiene-gate secret-safe-working-tree-clean-gate-self-test runtime-eventflow-budget-gate i2c-sdk-bug-avoidance-gate hotpath-zero-alloc-gate actor-module-consistency descriptor-contracts route-registry-integration-gate private-repo-secrets-policy release-evidence-contracts release-report-consistency-gate qos-contracts memory-budget ds18b20-driver-test bh1750-driver-test bh1750-actor-test runtime-soak-contract-self-test sdk-memory-stack-regression-self-test release-archive-workflow-contract-gate release-archive-content-scope-gate release-archive-self-clean-gate host-sanitize-drivers-test host-test property-test
	@echo "quality-gate passed"

release-gate: quality-gate docgen docs secret-safe-working-tree-clean-gate
	@echo "release-gate passed"

release-prearchive-gate: evidence-redaction-check private-repo-secrets-policy release-evidence-contracts release-report-consistency-gate routegen-check static-contracts actor-module-consistency descriptor-contracts route-registry-integration-gate patch-hygiene-gate host-test property-test host-strict-test host-sanitize-test release-archive-workflow-contract-gate release-archive-content-scope-gate release-archive-self-clean-gate secret-safe-working-tree-clean-gate
	@echo "release-prearchive-gate passed"

release-archive: release-prearchive-gate
	./tools/fw release-archive --from-make

production-release-gate: release-gate sdk-matrix-check sdk-matrix-warning-policy-self-test sdk-build-workflow-contract-gate sdk-memory-matrix sdk-memory-stack-regression-self-test
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
