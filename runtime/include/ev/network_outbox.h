#ifndef EV_NETWORK_OUTBOX_H
#define EV_NETWORK_OUTBOX_H

#include <stddef.h>
#include <stdint.h>
#include "ev/result.h"

#ifndef EV_NETWORK_OUTBOX_CAPACITY
#define EV_NETWORK_OUTBOX_CAPACITY 8U
#endif
#ifndef EV_NETWORK_PAYLOAD_BYTES
#define EV_NETWORK_PAYLOAD_BYTES 64U
#endif

typedef enum {
    EV_NETWORK_MSG_TELEMETRY_LATEST = 0,
    EV_NETWORK_MSG_TELEMETRY_PERIODIC,
    EV_NETWORK_MSG_COMMAND_RESPONSE,
    EV_NETWORK_MSG_FAULT_CRITICAL,
    EV_NETWORK_MSG_CATEGORY_COUNT
} ev_network_msg_category_t;

typedef struct {
    size_t category_limit[EV_NETWORK_MSG_CATEGORY_COUNT];
    uint8_t drop_oldest[EV_NETWORK_MSG_CATEGORY_COUNT];
} ev_network_backpressure_policy_t;

typedef struct {
    ev_network_msg_category_t category;
    uint16_t size;
    uint8_t payload[EV_NETWORK_PAYLOAD_BYTES];
} ev_network_outbox_item_t;

typedef struct {
    ev_network_outbox_item_t items[EV_NETWORK_OUTBOX_CAPACITY];
    size_t head;
    size_t count;
    size_t queued_by_category[EV_NETWORK_MSG_CATEGORY_COUNT];
    uint32_t accepted;
    uint32_t rejected;
    uint32_t dropped;
} ev_network_outbox_t;

void ev_network_outbox_init(ev_network_outbox_t *outbox);
ev_result_t ev_network_outbox_push(ev_network_outbox_t *outbox, const ev_network_backpressure_policy_t *policy, ev_network_msg_category_t category, const uint8_t *payload, size_t size);
ev_result_t ev_network_outbox_pop(ev_network_outbox_t *outbox, ev_network_outbox_item_t *out_item);

#endif
