#include "ev/mcp23008_actor.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/publish.h"

#define EV_MCP23008_REG_IODIR 0x00U
#define EV_MCP23008_REG_IOCON 0x05U
#define EV_MCP23008_REG_GPPU 0x06U
#define EV_MCP23008_REG_GPIO 0x09U
#define EV_MCP23008_REG_OLAT 0x0AU
#define EV_MCP23008_INPUT_DIR_MASK 0x0FU
#define EV_MCP23008_INPUT_PULLUP_MASK 0x0FU
#define EV_MCP23008_OUTPUT_SHIFT 4U
#define EV_MCP23008_IOCON_ODR 0x04U

static uint8_t ev_mcp23008_actor_normalize_buttons(uint8_t raw_gpio)
{
    return (uint8_t)((~raw_gpio) & EV_MCP23008_BUTTON_MASK);
}

static uint8_t ev_mcp23008_actor_build_olat(uint8_t logical_led_mask)
{
    return (uint8_t)((logical_led_mask & EV_MCP23008_LED_MASK) << EV_MCP23008_OUTPUT_SHIFT);
}

static bool ev_mcp23008_actor_write_reg(ev_mcp23008_actor_ctx_t *ctx, uint8_t reg, uint8_t value)
{
    const uint8_t payload = value;
    ev_i2c_status_t status;

    if ((ctx == NULL) || (ctx->i2c_port == NULL) || (ctx->i2c_port->write_regs == NULL)) {
        return false;
    }

    status = ctx->i2c_port->write_regs(ctx->i2c_port->ctx,
                                       ctx->port_num,
                                       ctx->device_address_7bit,
                                       reg,
                                       &payload,
                                       1U);
    return status == EV_I2C_OK;
}

static bool ev_mcp23008_actor_read_gpio(ev_mcp23008_actor_ctx_t *ctx, uint8_t *out_gpio)
{
    ev_i2c_status_t status;

    if ((ctx == NULL) || (out_gpio == NULL) || (ctx->i2c_port == NULL) || (ctx->i2c_port->read_regs == NULL)) {
        return false;
    }

    status = ctx->i2c_port->read_regs(ctx->i2c_port->ctx,
                                      ctx->port_num,
                                      ctx->device_address_7bit,
                                      EV_MCP23008_REG_GPIO,
                                      out_gpio,
                                      1U);
    return status == EV_I2C_OK;
}

static ev_result_t ev_mcp23008_actor_publish_input_changed(ev_mcp23008_actor_ctx_t *ctx,
                                                           uint8_t pressed_mask,
                                                           uint8_t changed_mask)
{
    ev_mcp23008_input_payload_t payload;
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    payload.pressed_mask = (uint8_t)(pressed_mask & EV_MCP23008_BUTTON_MASK);
    payload.changed_mask = (uint8_t)(changed_mask & EV_MCP23008_BUTTON_MASK);

    rc = ev_msg_init_publish(&msg, EV_MCP23008_INPUT_CHANGED, ACT_MCP23008);
    if (rc == EV_OK) {
        rc = ev_msg_set_inline_payload(&msg, &payload, sizeof(payload));
    }
    if (rc == EV_OK) {
        rc = ev_publish(&msg, ctx->deliver, ctx->deliver_context, NULL);
    }

    dispose_rc = ev_msg_dispose(&msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        rc = dispose_rc;
    }

    return rc;
}

static ev_result_t ev_mcp23008_actor_publish_ready(ev_mcp23008_actor_ctx_t *ctx)
{
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_init_publish(&msg, EV_MCP23008_READY, ACT_MCP23008);
    if (rc == EV_OK) {
        rc = ev_publish(&msg, ctx->deliver, ctx->deliver_context, NULL);
    }

    dispose_rc = ev_msg_dispose(&msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        rc = dispose_rc;
    }

    return rc;
}

static ev_result_t ev_mcp23008_actor_try_configure(ev_mcp23008_actor_ctx_t *ctx)
{
    uint8_t raw_gpio = 0U;
    ev_result_t rc;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ctx->configured = false;
    ctx->inputs_valid = false;

    if (!ev_mcp23008_actor_write_reg(ctx, EV_MCP23008_REG_IOCON, EV_MCP23008_IOCON_ODR)) {
        return EV_OK;
    }
    if (!ev_mcp23008_actor_write_reg(ctx, EV_MCP23008_REG_IODIR, EV_MCP23008_INPUT_DIR_MASK)) {
        return EV_OK;
    }
    if (!ev_mcp23008_actor_write_reg(ctx, EV_MCP23008_REG_GPPU, EV_MCP23008_INPUT_PULLUP_MASK)) {
        return EV_OK;
    }
    if (!ev_mcp23008_actor_write_reg(ctx, EV_MCP23008_REG_OLAT, ev_mcp23008_actor_build_olat(ctx->output_shadow))) {
        return EV_OK;
    }
    if (!ev_mcp23008_actor_read_gpio(ctx, &raw_gpio)) {
        return EV_OK;
    }

    ctx->input_shadow = ev_mcp23008_actor_normalize_buttons(raw_gpio);
    ctx->inputs_valid = true;
    ctx->configured = true;

    rc = ev_mcp23008_actor_publish_ready(ctx);
    if (rc != EV_OK) {
        ctx->configured = false;
        ctx->inputs_valid = false;
        return rc;
    }

    return EV_OK;
}

static ev_result_t ev_mcp23008_actor_handle_boot(ev_mcp23008_actor_ctx_t *ctx)
{
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    return ev_mcp23008_actor_try_configure(ctx);
}

static ev_result_t ev_mcp23008_actor_handle_tick(ev_mcp23008_actor_ctx_t *ctx)
{
    uint8_t raw_gpio = 0U;
    uint8_t pressed_mask;
    uint8_t changed_mask;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (!ctx->configured) {
        ev_result_t rc = ev_mcp23008_actor_try_configure(ctx);

        if (rc != EV_OK) {
            return rc;
        }
        if (!ctx->configured) {
            return EV_OK;
        }
    }
    if (!ev_mcp23008_actor_read_gpio(ctx, &raw_gpio)) {
        ctx->configured = false;
        ctx->inputs_valid = false;
        return EV_OK;
    }

    pressed_mask = ev_mcp23008_actor_normalize_buttons(raw_gpio);
    if (!ctx->inputs_valid) {
        ctx->input_shadow = pressed_mask;
        ctx->inputs_valid = true;
        return EV_OK;
    }

    changed_mask = (uint8_t)(pressed_mask ^ ctx->input_shadow);
    if (changed_mask == 0U) {
        return EV_OK;
    }

    ctx->input_shadow = pressed_mask;
    return ev_mcp23008_actor_publish_input_changed(ctx, pressed_mask, changed_mask);
}

static ev_result_t ev_mcp23008_actor_handle_led_cmd(ev_mcp23008_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_panel_led_set_cmd_t *cmd;
    uint8_t valid_mask;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    cmd = (const ev_panel_led_set_cmd_t *)ev_msg_payload_data(msg);
    if ((cmd == NULL) || (ev_msg_payload_size(msg) != sizeof(*cmd))) {
        return EV_ERR_CONTRACT;
    }

    valid_mask = (uint8_t)(cmd->valid_mask & EV_MCP23008_LED_MASK);
    ctx->output_shadow = (uint8_t)((ctx->output_shadow & (uint8_t)(~valid_mask)) |
                                   (cmd->value_mask & valid_mask & EV_MCP23008_LED_MASK));

    if (!ctx->configured) {
        return EV_OK;
    }

    if (!ev_mcp23008_actor_write_reg(ctx, EV_MCP23008_REG_OLAT, ev_mcp23008_actor_build_olat(ctx->output_shadow))) {
        ctx->configured = false;
        ctx->inputs_valid = false;
        return EV_OK;
    }

    return EV_OK;
}

ev_result_t ev_mcp23008_actor_init(ev_mcp23008_actor_ctx_t *ctx,
                                   ev_i2c_port_t *i2c_port,
                                   ev_i2c_port_num_t port_num,
                                   uint8_t device_address_7bit,
                                   ev_delivery_fn_t deliver,
                                   void *deliver_context)
{
    if ((ctx == NULL) || (i2c_port == NULL) || (i2c_port->read_regs == NULL) || (i2c_port->write_regs == NULL) ||
        (deliver == NULL) || (deliver_context == NULL) || (device_address_7bit > 0x7FU)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->i2c_port = i2c_port;
    ctx->port_num = port_num;
    ctx->device_address_7bit = device_address_7bit;
    ctx->deliver = deliver;
    ctx->deliver_context = deliver_context;
    return EV_OK;
}

ev_result_t ev_mcp23008_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_mcp23008_actor_ctx_t *ctx = (ev_mcp23008_actor_ctx_t *)actor_context;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_BOOT_COMPLETED:
        return ev_mcp23008_actor_handle_boot(ctx);

    case EV_TICK_100MS:
        return ev_mcp23008_actor_handle_tick(ctx);

    case EV_PANEL_LED_SET_CMD:
        return ev_mcp23008_actor_handle_led_cmd(ctx, msg);

    default:
        return EV_ERR_CONTRACT;
    }
}
