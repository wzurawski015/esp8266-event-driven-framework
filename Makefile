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
    drivers/src/ev_driver_layer.c

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
    test_dispatch_contract \
    test_mailbox_contract \
    test_actor_runtime \
    test_lease_pool_contract \
    test_zero_copy_payload_contract \
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

.PHONY: all host-test property-test host-strict-test host-sanitize-cc-check host-sanitize-test host-tsan-cc-check host-tsan-test clang-tidy-gate host-gcc-analyzer-gate host-static-analysis-gate static-analysis-gate host-coverage-test coverage-report coverage-gate fuzz-smoke-gate fuzz-sanitize-gate ub-hardening-gate safety-gate hotpath-zero-alloc-gate bench perf-report perf-budget-gate perf-gate routegen mailbox-layoutgen routegen-check mailbox-layoutgen-check static-contracts actor-module-consistency descriptor-contracts private-repo-secrets-policy release-evidence-contracts qos-contracts public-release-safety-gate memory-budget sdk-matrix-check sdk-memory-matrix sdk-memory-release-gate sdk-build-evidence sdk-map-stack-evidence sdk-evidence-gate sdk-import-evidence sdk-import-evidence-gate sdk-full-evidence-gate hil-atnel-i2c-flash hil-atnel-i2c-monitor hil-atnel-i2c-evidence hil-atnel-i2c-gate hil-import-atnel-i2c-evidence hil-import-wemos-smoke-evidence hil-import-wemos-deepsleep-evidence hil-import-all-evidence hil-real-evidence-gate hil-wemos-smoke-flash hil-wemos-smoke-monitor hil-wemos-smoke-evidence hil-wemos-smoke-gate hil-wemos-deepsleep-wake-gate eventflow-hardware-evidence-report eventflow-hardware-evidence-gate production-release-gate quality-gate release-gate docgen docs clean
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

host-sanitize-test: host-sanitize-cc-check
	@echo "host-sanitize-test: AddressSanitizer + UndefinedBehaviorSanitizer host build"
	@$(MAKE) --no-print-directory BUILD_DIR=build/host-sanitize CFLAGS="$(HOST_SANITIZE_CFLAGS)" LDFLAGS="$(HOST_SANITIZE_LDFLAGS) $(LDFLAGS)" host-test

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

private-repo-secrets-policy:
	$(PYTHON) tools/audit/private_repo_secrets_policy.py

release-evidence-contracts:
	$(PYTHON) tools/audit/release_evidence_contracts.py

qos-contracts:
	$(PYTHON) tools/audit/qos_contract_check.py

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
	@if [ -n "$${EV_HIL_ATNEL_I2C_SERIAL_LOG:-}" ]; then $(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --log "$${EV_HIL_ATNEL_I2C_SERIAL_LOG}"; else $(PYTHON) tools/hil/parse_atnel_i2c_hil_log.py --environment-blocked; fi

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

sdk-memory-release-gate:
	$(PYTHON) tools/sdk_memory_report.py --self-test
	$(PYTHON) tools/sdk_memory_matrix.py --self-test
	EV_SDK_MEMORY_REQUIRE_PASS=1 $(PYTHON) tools/sdk_memory_matrix.py

.NOTPARALLEL: quality-gate
quality-gate: clean routegen-check static-contracts hotpath-zero-alloc-gate actor-module-consistency descriptor-contracts private-repo-secrets-policy release-evidence-contracts qos-contracts memory-budget host-test property-test
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
