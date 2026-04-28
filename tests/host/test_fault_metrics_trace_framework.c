#include <assert.h>

#include "ev/fault_bus.h"
#include "ev/metrics_registry.h"
#include "ev/trace_ring.h"

int main(void)
{
    ev_fault_registry_t faults;
    ev_fault_payload_t fault;
    ev_fault_snapshot_t snap;
    ev_metric_registry_t metrics;
    ev_metric_sample_t sample;
    ev_trace_ring_t trace;
    ev_trace_record_t rec;

    ev_fault_registry_init(&faults);
    fault.fault_id = EV_FAULT_MAILBOX_OVERFLOW;
    fault.severity = EV_FAULT_SEV_ERROR;
    fault.source_actor = ACT_APP;
    fault.triggering_event = EV_BOOT_STARTED;
    fault.source_module = 0U;
    fault.timestamp_ms = 1U;
    fault.arg0 = 2U;
    fault.arg1 = 3U;
    fault.counter = 0U;
    fault.flags = 0U;
    assert(ev_fault_emit(&faults, &fault) == EV_OK);
    assert(ev_fault_emit(&faults, &fault) == EV_OK);
    assert(ev_fault_snapshot(&faults, &snap) == EV_OK);
    assert(snap.count == 1U);
    assert(snap.records[0].counter == 2U);
    assert(snap.coalesced == 1U);

    ev_metric_registry_init(&metrics);
    assert(ev_metric_increment(&metrics, EV_METRIC_POST_OK, 2U) == EV_OK);
    assert(ev_metric_read(&metrics, EV_METRIC_POST_OK, &sample) == EV_OK);
    assert(sample.value == 2U);

    ev_trace_ring_init(&trace);
    rec.timestamp_us = 1U;
    rec.event_id = EV_BOOT_STARTED;
    rec.source_actor = ACT_APP;
    rec.target_actor = ACT_DIAG;
    rec.result = EV_OK;
    rec.qos = EV_ROUTE_QOS_CRITICAL;
    rec.queue_depth = 0U;
    rec.flags = 0U;
    assert(ev_trace_record(&trace, &rec) == EV_OK);
    assert(ev_trace_pending(&trace) == 1U);
    return 0;
}
