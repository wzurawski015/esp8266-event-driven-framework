#include "ev/metrics_registry.h"

#include <string.h>

static int ev_metric_id_valid(ev_metric_id_t metric_id)
{
    return ((metric_id >= 0) && ((size_t)metric_id < (size_t)EV_METRIC_COUNT));
}

void ev_metric_registry_init(ev_metric_registry_t *registry)
{
    if (registry != NULL) {
        (void)memset(registry, 0, sizeof(*registry));
    }
}

ev_result_t ev_metric_increment(ev_metric_registry_t *registry, ev_metric_id_t metric_id, uint32_t delta)
{
    if ((registry == NULL) || (ev_metric_id_valid(metric_id) == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    registry->values[metric_id] += delta;
    return EV_OK;
}

ev_result_t ev_metric_set_gauge(ev_metric_registry_t *registry, ev_metric_id_t metric_id, uint32_t value)
{
    if ((registry == NULL) || (ev_metric_id_valid(metric_id) == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    if (value > registry->values[metric_id]) {
        registry->values[metric_id] = value;
    }
    return EV_OK;
}

ev_result_t ev_metric_read(const ev_metric_registry_t *registry, ev_metric_id_t metric_id, ev_metric_sample_t *out_sample)
{
    if ((registry == NULL) || (out_sample == NULL) || (ev_metric_id_valid(metric_id) == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    out_sample->id = metric_id;
    out_sample->value = registry->values[metric_id];
    out_sample->kind = EV_METRIC_KIND_COUNTER;
    return EV_OK;
}

ev_result_t ev_metric_snapshot(const ev_metric_registry_t *registry, ev_metric_sample_t *out_samples, size_t capacity, size_t *out_count)
{
    size_t i;
    size_t n;

    if ((registry == NULL) || (out_samples == NULL) || (out_count == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    n = ((size_t)EV_METRIC_COUNT < capacity) ? (size_t)EV_METRIC_COUNT : capacity;
    for (i = 0U; i < n; ++i) {
        out_samples[i].id = (ev_metric_id_t)i;
        out_samples[i].value = registry->values[i];
        out_samples[i].kind = EV_METRIC_KIND_COUNTER;
    }
    *out_count = n;
    return ((size_t)EV_METRIC_COUNT <= capacity) ? EV_OK : EV_ERR_PARTIAL;
}
