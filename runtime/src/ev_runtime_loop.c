#include "ev/runtime_loop.h"

#include <string.h>

#define EV_RUNTIME_LOOP_DEFAULT_PUMP_CALLS 10U
#define EV_RUNTIME_LOOP_DEFAULT_MESSAGES 32U
#define EV_RUNTIME_LOOP_DEFAULT_TURNS 10U
#define EV_RUNTIME_LOOP_DEFAULT_IRQ_SAMPLES 16U
#define EV_RUNTIME_LOOP_DEFAULT_NET_SAMPLES 16U
#define EV_RUNTIME_LOOP_DEFAULT_TIMER_BUDGET 1U
#define EV_RUNTIME_LOOP_DEFAULT_SCHEDULER_TURNS 4U

void ev_runtime_loop_policy_default(ev_runtime_loop_policy_t *policy)
{
    if (policy != NULL) {
        (void)memset(policy, 0, sizeof(*policy));
        policy->max_pump_calls = EV_RUNTIME_LOOP_DEFAULT_PUMP_CALLS;
        policy->max_messages = EV_RUNTIME_LOOP_DEFAULT_MESSAGES;
        policy->max_turns = EV_RUNTIME_LOOP_DEFAULT_TURNS;
        policy->max_irq_samples = EV_RUNTIME_LOOP_DEFAULT_IRQ_SAMPLES;
        policy->max_net_samples = EV_RUNTIME_LOOP_DEFAULT_NET_SAMPLES;
        policy->timer_publish_budget = EV_RUNTIME_LOOP_DEFAULT_TIMER_BUDGET;
        policy->scheduler_turn_budget = EV_RUNTIME_LOOP_DEFAULT_SCHEDULER_TURNS;
        policy->skip_timers_when_scheduler_pending = 1U;
        policy->run_scheduler_after_timers = 1U;
    }
}

void ev_runtime_loop_report_reset(ev_runtime_loop_report_t *report)
{
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
        report->last_result = EV_OK;
    }
}

static uint8_t ev_runtime_loop_exhausted(const ev_runtime_loop_policy_t *policy, const ev_runtime_loop_report_t *report)
{
    if ((policy == NULL) || (report == NULL)) {
        return 1U;
    }
    return ((report->pump_calls >= policy->max_pump_calls) ||
            (report->messages >= policy->max_messages) ||
            (report->turns >= policy->max_turns) ||
            (report->irq_samples >= policy->max_irq_samples) ||
            (report->net_samples >= policy->max_net_samples)) ? 1U : 0U;
}

static ev_result_t ev_runtime_loop_drain_scheduler(ev_runtime_graph_t *graph,
                                                   const ev_runtime_loop_policy_t *policy,
                                                   ev_runtime_loop_report_t *report)
{
    ev_result_t rc;

    while ((ev_runtime_graph_scheduler_pending(graph) > 0U) && (report->exhausted == 0U)) {
        ev_system_pump_report_t pump_report;
        if (ev_runtime_loop_exhausted(policy, report) != 0U) {
            report->exhausted = 1U;
            break;
        }
        (void)memset(&pump_report, 0, sizeof(pump_report));
        rc = ev_runtime_graph_poll_scheduler_once(graph, policy->scheduler_turn_budget, &pump_report);
        report->pump_calls++;
        report->turns += (uint32_t)pump_report.turns_processed;
        report->messages += (uint32_t)pump_report.messages_processed;
        if ((rc == EV_OK) || (rc == EV_ERR_PARTIAL)) {
            report->exhausted = ev_runtime_loop_exhausted(policy, report);
            continue;
        }
        if (rc == EV_ERR_EMPTY) {
            return EV_OK;
        }
        report->last_result = rc;
        return rc;
    }
    return EV_OK;
}

static ev_result_t ev_runtime_loop_collect(ev_runtime_graph_t *graph,
                                           const ev_runtime_loop_policy_t *policy,
                                           const ev_runtime_loop_ports_t *ports,
                                           ev_runtime_loop_report_t *report)
{
    if ((ports != NULL) && (ports->collect_ingress != NULL)) {
        return ports->collect_ingress(graph, ports->collect_ctx, report, policy);
    }
    (void)graph;
    (void)policy;
    return EV_OK;
}

ev_result_t ev_runtime_loop_poll_once(ev_runtime_graph_t *graph,
                                      const ev_runtime_loop_policy_t *policy,
                                      const ev_runtime_loop_ports_t *ports,
                                      ev_runtime_loop_report_t *out_report)
{
    ev_runtime_loop_policy_t local_policy;
    ev_runtime_loop_report_t report;
    uint32_t start_ms = 0U;
    uint32_t end_ms = 0U;
    uint32_t now_ms = 0U;
    uint8_t have_timing = 0U;
    ev_result_t rc = EV_OK;

    if ((graph == NULL) || (ports == NULL) || (ports->now_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (policy != NULL) {
        local_policy = *policy;
    } else {
        ev_runtime_loop_policy_default(&local_policy);
    }
    if ((local_policy.scheduler_turn_budget == 0U) || (local_policy.timer_publish_budget == 0U)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_runtime_loop_report_reset(&report);
    report.pending_before = (uint32_t)ev_runtime_graph_scheduler_pending(graph);
    if (ports->now_ms(ports->now_ctx, &start_ms) == EV_OK) {
        have_timing = 1U;
    }

    for (;;) {
        uint32_t before_irq = report.irq_samples;
        uint32_t before_net = report.net_samples;
        rc = ev_runtime_loop_collect(graph, &local_policy, ports, &report);
        if (rc != EV_OK) {
            goto finalize;
        }
        report.exhausted = ev_runtime_loop_exhausted(&local_policy, &report);
        rc = ev_runtime_loop_drain_scheduler(graph, &local_policy, &report);
        if (rc != EV_OK) {
            goto finalize;
        }
        if ((report.exhausted != 0U) ||
            ((report.irq_samples == before_irq) && (report.net_samples == before_net))) {
            break;
        }
    }

    if (report.exhausted == 0U) {
        rc = ports->now_ms(ports->now_ctx, &now_ms);
        if (rc != EV_OK) {
            goto finalize;
        }
        for (;;) {
            uint32_t before_published = ev_runtime_graph_timer_published_count(graph);
            size_t published = 0U;
            if ((local_policy.skip_timers == 0U) &&
                ((local_policy.skip_timers_when_scheduler_pending == 0U) ||
                 (ev_runtime_graph_scheduler_pending(graph) == 0U))) {
                published = ev_runtime_graph_publish_due_timers(graph,
                                                                now_ms,
                                                                ports->timer_delivery,
                                                                ports->timer_delivery_ctx,
                                                                local_policy.timer_publish_budget);
                report.timers_published += (uint32_t)published;
            }
            report.exhausted = ev_runtime_loop_exhausted(&local_policy, &report);
            if ((local_policy.run_scheduler_after_timers != 0U) && (report.exhausted == 0U)) {
                rc = ev_runtime_loop_drain_scheduler(graph, &local_policy, &report);
                if (rc != EV_OK) {
                    goto finalize;
                }
            }
            if ((report.exhausted != 0U) ||
                (published == 0U) ||
                (ev_runtime_graph_timer_published_count(graph) == before_published)) {
                break;
            }
        }
    }

finalize:
    report.pending_after = (uint32_t)ev_runtime_graph_scheduler_pending(graph);
    if ((have_timing != 0U) && (ports->now_ms(ports->now_ctx, &end_ms) == EV_OK)) {
        report.elapsed_ms = end_ms - start_ms;
    }
    if ((rc == EV_OK) && (report.exhausted != 0U)) {
        uint8_t extra_pending = 0U;
        if ((ports->work_pending != NULL) &&
            (ports->work_pending(graph, ports->work_pending_ctx, end_ms, &report, &extra_pending) != EV_OK)) {
            extra_pending = 1U;
        }
        if ((report.pending_after > 0U) || (extra_pending != 0U)) {
            report.partial = 1U;
            rc = EV_ERR_PARTIAL;
        }
    }
    report.last_result = rc;
    if (out_report != NULL) {
        *out_report = report;
    }
    return rc;
}
