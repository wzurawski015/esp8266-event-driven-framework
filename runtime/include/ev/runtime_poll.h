#ifndef EV_RUNTIME_POLL_H
#define EV_RUNTIME_POLL_H

#include <stddef.h>
#include <stdint.h>
#include "ev/result.h"
#include "ev/runtime_graph.h"

typedef struct {
    size_t timers_published;
    size_t actors_pumped;
    size_t domains_pumped;
    size_t messages_processed;
    ev_result_t last_result;
} ev_runtime_poll_report_t;

ev_result_t ev_runtime_poll_once(ev_runtime_graph_t *graph, uint32_t now_ms, size_t actor_budget, ev_runtime_poll_report_t *out_report);

#endif
