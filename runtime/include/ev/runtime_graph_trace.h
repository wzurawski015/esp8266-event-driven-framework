#ifndef EV_RUNTIME_GRAPH_TRACE_H
#define EV_RUNTIME_GRAPH_TRACE_H

#include <stddef.h>

#include "ev/runtime_graph.h"
#include "ev/trace_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

ev_result_t ev_runtime_graph_trace_record(ev_runtime_graph_t *graph, const ev_trace_record_t *record);
void ev_runtime_graph_trace_clear(ev_runtime_graph_t *graph);
size_t ev_runtime_graph_trace_drain(ev_runtime_graph_t *graph, ev_trace_record_t *out_records, size_t max_records);

#ifdef __cplusplus
}
#endif

#endif /* EV_RUNTIME_GRAPH_TRACE_H */
