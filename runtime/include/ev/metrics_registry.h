#ifndef EV_METRICS_REGISTRY_H
#define EV_METRICS_REGISTRY_H

#include <stddef.h>
#include <stdint.h>
#include "ev/result.h"

typedef enum {
#define EV_METRIC(name, kind, summary) name,
#include "metrics.def"
#undef EV_METRIC
    EV_METRIC_COUNT
} ev_metric_id_t;

typedef enum {
    EV_METRIC_KIND_COUNTER = 0,
    EV_METRIC_KIND_GAUGE = 1
} ev_metric_kind_t;

typedef struct {
    ev_metric_id_t id;
    uint32_t value;
    ev_metric_kind_t kind;
} ev_metric_sample_t;

typedef struct {
    uint32_t values[EV_METRIC_COUNT];
} ev_metric_registry_t;

void ev_metric_registry_init(ev_metric_registry_t *registry);
ev_result_t ev_metric_increment(ev_metric_registry_t *registry, ev_metric_id_t metric_id, uint32_t delta);
ev_result_t ev_metric_set_gauge(ev_metric_registry_t *registry, ev_metric_id_t metric_id, uint32_t value);
ev_result_t ev_metric_read(const ev_metric_registry_t *registry, ev_metric_id_t metric_id, ev_metric_sample_t *out_sample);
ev_result_t ev_metric_snapshot(const ev_metric_registry_t *registry, ev_metric_sample_t *out_samples, size_t capacity, size_t *out_count);

#endif
