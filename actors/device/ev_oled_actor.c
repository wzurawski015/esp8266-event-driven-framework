#include "ev/oled_actor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/compiler.h"
#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/publish.h"

#define EV_OLED_CONTROL_CMD 0x00U
#define EV_OLED_CONTROL_DATA 0x40U
#define EV_OLED_GLYPH_WIDTH 5U
#define EV_OLED_GLYPH_SPACING 1U
#define EV_OLED_CELL_ADVANCE (EV_OLED_GLYPH_WIDTH + EV_OLED_GLYPH_SPACING)
#define EV_OLED_DATA_CHUNK_BYTES 32U
#define EV_OLED_SH1106_COLUMN_OFFSET 2U
#define EV_OLED_SSD1306_72X40_COLUMN_OFFSET 28U

static const uint8_t k_ev_oled_font_5x7[][EV_OLED_GLYPH_WIDTH] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* # */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* $ */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
    {0x14, 0x08, 0x3E, 0x08, 0x14}, /* * */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
    {0x08, 0x14, 0x22, 0x41, 0x00}, /* < */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
    {0x00, 0x41, 0x22, 0x14, 0x08}, /* > */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* @ */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x7F, 0x41, 0x41, 0x00}, /* [ */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* \\ */
    {0x00, 0x41, 0x41, 0x7F, 0x00}, /* ] */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* ^ */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* _ */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* ` */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* a */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* b */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* c */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* d */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* e */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* f */
    {0x08, 0x14, 0x54, 0x54, 0x3C}, /* g */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* h */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* i */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* j */
    {0x7F, 0x10, 0x28, 0x44, 0x00}, /* k */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* l */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* m */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* n */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* o */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* p */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* q */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* r */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* s */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* t */
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* u */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* v */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* w */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* x */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* y */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* z */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* { */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* | */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* } */
    {0x08, 0x04, 0x08, 0x10, 0x08}  /* ~ */
};

static const uint8_t k_ev_oled_ssd1306_init[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x27, 0xD3, 0x00, 0xAD,
    0x30, 0x8D, 0x14, 0x40, 0xA6, 0xA4, 0x20, 0x00,
    0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xAF, 0xD9, 0x22,
    0xDB, 0x20, 0x2E, 0xAF
};

static const uint8_t k_ev_oled_sh1106_init[] = {
    0xAE, 0xA6, 0xD5, 0x80, 0xA8, 0x3F, 0xDA, 0x12,
    0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1,
    0xC8, 0x81, 0x7F, 0xD9, 0x22, 0xDB, 0x40, 0xA4,
    0xAF
};

static const uint8_t *ev_oled_actor_glyph_for_char(char c)
{
    uint8_t index = (uint8_t)c;

    if ((index < 32U) || (index > 126U)) {
        index = (uint8_t)'?';
    }

    return k_ev_oled_font_5x7[index - 32U];
}

static size_t ev_oled_actor_bounded_strlen(const char *text, size_t max_len)
{
    size_t len = 0U;

    if (text == NULL) {
        return 0U;
    }

    while ((len < max_len) && (text[len] != '\0')) {
        ++len;
    }

    return len;
}

static ev_result_t ev_oled_actor_publish_ready(ev_oled_actor_ctx_t *ctx)
{
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_init_publish(&msg, EV_OLED_READY, ACT_OLED);
    if (rc == EV_OK) {
        rc = ev_publish(&msg, ctx->deliver, ctx->deliver_context, NULL);
    }

    dispose_rc = ev_msg_dispose(&msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        rc = dispose_rc;
    }

    return rc;
}

static ev_i2c_status_t ev_oled_actor_write_stream(ev_oled_actor_ctx_t *ctx, const uint8_t *data, size_t data_len)
{

    if ((ctx == NULL) || (ctx->i2c_port == NULL) || (ctx->i2c_port->write_stream == NULL)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }
    return ctx->i2c_port->write_stream(ctx->i2c_port->ctx,
                                       ctx->port_num,
                                       ctx->device_address_7bit,
                                       data,
                                       data_len);
}

static ev_result_t ev_oled_actor_send_commands(ev_oled_actor_ctx_t *ctx, const uint8_t *commands, size_t command_count)
{
    uint8_t tx[1U + EV_OLED_DATA_CHUNK_BYTES];
    size_t offset = 0U;

    if ((ctx == NULL) || ((commands == NULL) && (command_count > 0U))) {
        return EV_ERR_INVALID_ARG;
    }

    tx[0] = EV_OLED_CONTROL_CMD;
    while (offset < command_count) {
        size_t chunk = command_count - offset;
        ev_i2c_status_t status;

        if (chunk > EV_OLED_DATA_CHUNK_BYTES) {
            chunk = EV_OLED_DATA_CHUNK_BYTES;
        }

        memcpy(&tx[1], &commands[offset], chunk);
        status = ev_oled_actor_write_stream(ctx, tx, chunk + 1U);
        ctx->last_i2c_status = status;
        ctx->stats.last_i2c_status = status;
        if (status != EV_I2C_OK) {
            return EV_ERR_STATE;
        }
        offset += chunk;
    }

    return EV_OK;
}

static ev_result_t ev_oled_actor_send_data(ev_oled_actor_ctx_t *ctx, const uint8_t *data, size_t data_len)
{
    uint8_t tx[1U + EV_OLED_DATA_CHUNK_BYTES];
    size_t offset = 0U;

    if ((ctx == NULL) || ((data == NULL) && (data_len > 0U))) {
        return EV_ERR_INVALID_ARG;
    }

    tx[0] = EV_OLED_CONTROL_DATA;
    while (offset < data_len) {
        size_t chunk = data_len - offset;
        ev_i2c_status_t status;

        if (chunk > EV_OLED_DATA_CHUNK_BYTES) {
            chunk = EV_OLED_DATA_CHUNK_BYTES;
        }

        memcpy(&tx[1], &data[offset], chunk);
        status = ev_oled_actor_write_stream(ctx, tx, chunk + 1U);
        ctx->last_i2c_status = status;
        ctx->stats.last_i2c_status = status;
        if (status != EV_I2C_OK) {
            return EV_ERR_STATE;
        }
        offset += chunk;
    }

    return EV_OK;
}


static void ev_oled_actor_reset_dirty(ev_oled_actor_ctx_t *ctx)
{
    uint8_t page;

    if (ctx == NULL) {
        return;
    }

    ctx->pending_flush = false;
    ctx->dirty_page_mask = 0U;
    for (page = 0U; page < EV_OLED_PAGE_COUNT; ++page) {
        ctx->dirty_column_start[page] = EV_OLED_WIDTH;
        ctx->dirty_column_end[page] = 0U;
    }
}

static void ev_oled_actor_mark_dirty(ev_oled_actor_ctx_t *ctx, uint8_t page, uint8_t start_column, uint8_t end_column)
{
    if ((ctx == NULL) || (page >= EV_OLED_PAGE_COUNT) || (start_column >= end_column) || (end_column > EV_OLED_WIDTH)) {
        return;
    }

    ctx->pending_flush = true;
    ctx->dirty_page_mask = (uint8_t)(ctx->dirty_page_mask | (uint8_t)(1U << page));
    if (start_column < ctx->dirty_column_start[page]) {
        ctx->dirty_column_start[page] = start_column;
    }
    if (end_column > ctx->dirty_column_end[page]) {
        ctx->dirty_column_end[page] = end_column;
    }
}

static void ev_oled_actor_schedule_retry(ev_oled_actor_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->state = EV_OLED_STATE_ERROR;
    ctx->retry_due_tick = ctx->tick_counter + ctx->retry_delay_ticks;
    ++ctx->stats.retries_scheduled;
}

static ev_result_t ev_oled_actor_apply_address_window(ev_oled_actor_ctx_t *ctx,
                                                      uint8_t page,
                                                      uint8_t start_column,
                                                      uint8_t end_column,
                                                      const uint8_t *page_data)
{
    if ((ctx == NULL) || (page >= EV_OLED_PAGE_COUNT) || (start_column >= end_column) || (end_column > EV_OLED_WIDTH) ||
        (page_data == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    if (ctx->controller == EV_OLED_CONTROLLER_SH1106) {
        uint8_t current = start_column;

        while (current < end_column) {
            uint8_t physical_column = (uint8_t)(current + EV_OLED_SH1106_COLUMN_OFFSET);
            const uint8_t commands[] = {
                (uint8_t)(0xB0U | page),
                (uint8_t)(0x00U | (physical_column & 0x0FU)),
                (uint8_t)(0x10U | ((physical_column >> 4U) & 0x0FU))
            };
            ev_result_t rc = ev_oled_actor_send_commands(ctx, commands, EV_ARRAY_LEN(commands));
            if (rc != EV_OK) {
                return rc;
            }

            {
                size_t chunk = (size_t)(end_column - current);
                if (chunk > EV_OLED_DATA_CHUNK_BYTES) {
                    chunk = EV_OLED_DATA_CHUNK_BYTES;
                }
                rc = ev_oled_actor_send_data(ctx, &page_data[current], chunk);
                if (rc != EV_OK) {
                    return rc;
                }
                current = (uint8_t)(current + (uint8_t)chunk);
            }
        }

        return EV_OK;
    }

    {
        const uint8_t commands[] = {
            0x21U,
            (uint8_t)(start_column + EV_OLED_SSD1306_72X40_COLUMN_OFFSET),
            (uint8_t)((end_column - 1U) + EV_OLED_SSD1306_72X40_COLUMN_OFFSET),
            0x22U,
            page,
            page
        };
        ev_result_t rc = ev_oled_actor_send_commands(ctx, commands, EV_ARRAY_LEN(commands));
        if (rc != EV_OK) {
            return rc;
        }
    }

    return ev_oled_actor_send_data(ctx, &page_data[start_column], (size_t)(end_column - start_column));
}

static ev_result_t ev_oled_actor_sync_full_framebuffer(ev_oled_actor_ctx_t *ctx)
{
    uint8_t page;
    uint32_t baseline_successes;
    uint32_t baseline_attempts;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    baseline_attempts = ctx->stats.flush_attempts;
    baseline_successes = ctx->stats.flush_successes;
    ++ctx->stats.flush_attempts;

    for (page = 0U; page < EV_OLED_PAGE_COUNT; ++page) {
        ev_result_t rc = ev_oled_actor_apply_address_window(ctx, page, 0U, EV_OLED_WIDTH, ctx->framebuffer[page]);
        if (rc != EV_OK) {
            ctx->stats.flush_attempts = baseline_attempts + 1U;
            ctx->stats.flush_successes = baseline_successes;
            ++ctx->stats.flush_failures;
            ev_oled_actor_schedule_retry(ctx);
            return EV_OK;
        }
    }

    ctx->stats.flush_attempts = baseline_attempts + 1U;
    ctx->stats.flush_successes = baseline_successes + 1U;
    return EV_OK;
}

static ev_result_t ev_oled_actor_render_text_into(uint8_t framebuffer[EV_OLED_PAGE_COUNT][EV_OLED_WIDTH],
                                                  const ev_oled_display_text_cmd_t *cmd)
{
    size_t text_len;
    size_t idx;
    uint8_t column;
    uint8_t page;

    if ((framebuffer == NULL) || (cmd == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((cmd->page >= EV_OLED_PAGE_COUNT) || (cmd->column >= EV_OLED_WIDTH)) {
        return EV_ERR_OUT_OF_RANGE;
    }

    page = cmd->page;
    column = cmd->column;
    memset(&framebuffer[page][column], 0, EV_OLED_WIDTH - column);

    text_len = ev_oled_actor_bounded_strlen(cmd->text, EV_OLED_TEXT_MAX_CHARS);
    for (idx = 0U; idx < text_len; ++idx) {
        const uint8_t *glyph = ev_oled_actor_glyph_for_char(cmd->text[idx]);
        size_t glyph_col;
        uint8_t dst = (uint8_t)(column + (uint8_t)(idx * EV_OLED_CELL_ADVANCE));

        if (dst >= EV_OLED_WIDTH) {
            break;
        }

        for (glyph_col = 0U; glyph_col < EV_OLED_GLYPH_WIDTH; ++glyph_col) {
            uint8_t target = (uint8_t)(dst + (uint8_t)glyph_col);
            if (target >= EV_OLED_WIDTH) {
                break;
            }
            framebuffer[page][target] = glyph[glyph_col];
        }
    }

    return EV_OK;
}

static ev_result_t ev_oled_actor_prepare_scene_commit(ev_oled_actor_ctx_t *ctx, const ev_oled_scene_t *scene)
{
    uint8_t line;
    uint8_t page;
    uint8_t column;

    if ((ctx == NULL) || (scene == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (scene->column_offset >= EV_OLED_WIDTH) {
        return EV_ERR_OUT_OF_RANGE;
    }

    memset(ctx->staging_framebuffer, 0, sizeof(ctx->staging_framebuffer));

    if ((scene->flags & EV_OLED_SCENE_FLAG_VISIBLE) != 0U) {
        for (line = 0U; line < EV_OLED_SCENE_LINE_COUNT; ++line) {
            ev_oled_display_text_cmd_t cmd = {0};
            ev_result_t rc;

            cmd.page = (uint8_t)(scene->page_offset + line);
            cmd.column = scene->column_offset;
            if (cmd.page >= EV_OLED_PAGE_COUNT) {
                continue;
            }
            memcpy(cmd.text, scene->lines[line], sizeof(cmd.text));
            cmd.text[EV_OLED_TEXT_MAX_CHARS - 1U] = '\0';

            rc = ev_oled_actor_render_text_into(ctx->staging_framebuffer, &cmd);
            if (rc != EV_OK) {
                return rc;
            }
        }
    }

    ev_oled_actor_reset_dirty(ctx);
    for (page = 0U; page < EV_OLED_PAGE_COUNT; ++page) {
        for (column = 0U; column < EV_OLED_WIDTH; ++column) {
            if (ctx->staging_framebuffer[page][column] != ctx->framebuffer[page][column]) {
                ev_oled_actor_mark_dirty(ctx, page, column, (uint8_t)(column + 1U));
            }
        }
    }

    return EV_OK;
}

static ev_result_t ev_oled_actor_flush_pending(ev_oled_actor_ctx_t *ctx)
{
    uint8_t page;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ctx->pending_flush) {
        return EV_OK;
    }

    ++ctx->stats.flush_attempts;
    for (page = 0U; page < EV_OLED_PAGE_COUNT; ++page) {
        ev_result_t rc;
        uint8_t start_column;
        uint8_t end_column;

        if ((ctx->dirty_page_mask & (uint8_t)(1U << page)) == 0U) {
            continue;
        }

        start_column = ctx->dirty_column_start[page];
        end_column = ctx->dirty_column_end[page];
        if ((start_column >= end_column) || (end_column > EV_OLED_WIDTH)) {
            continue;
        }

        rc = ev_oled_actor_apply_address_window(ctx,
                                                page,
                                                start_column,
                                                end_column,
                                                ctx->staging_framebuffer[page]);
        if (rc != EV_OK) {
            ++ctx->stats.flush_failures;
            ev_oled_actor_schedule_retry(ctx);
            return EV_OK;
        }
    }

    memcpy(ctx->framebuffer, ctx->staging_framebuffer, sizeof(ctx->framebuffer));
    ev_oled_actor_reset_dirty(ctx);
    ++ctx->stats.flush_successes;
    return EV_OK;
}

static ev_result_t ev_oled_actor_try_initialize(ev_oled_actor_ctx_t *ctx)
{
    const uint8_t *init_sequence = NULL;
    size_t init_length = 0U;
    ev_result_t rc;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++ctx->stats.init_attempts;

    if (ctx->controller == EV_OLED_CONTROLLER_SH1106) {
        init_sequence = k_ev_oled_sh1106_init;
        init_length = EV_ARRAY_LEN(k_ev_oled_sh1106_init);
    } else {
        init_sequence = k_ev_oled_ssd1306_init;
        init_length = EV_ARRAY_LEN(k_ev_oled_ssd1306_init);
    }

    rc = ev_oled_actor_send_commands(ctx, init_sequence, init_length);
    if (rc != EV_OK) {
        ++ctx->stats.init_failures;
        ev_oled_actor_schedule_retry(ctx);
        return EV_OK;
    }

    rc = ev_oled_actor_sync_full_framebuffer(ctx);
    if (rc != EV_OK) {
        ++ctx->stats.init_failures;
        ev_oled_actor_schedule_retry(ctx);
        return EV_OK;
    }

    ctx->state = EV_OLED_STATE_READY;
    ++ctx->stats.init_successes;
    rc = ev_oled_actor_publish_ready(ctx);
    if (rc != EV_OK) {
        return rc;
    }
    if (ctx->pending_flush) {
        return ev_oled_actor_flush_pending(ctx);
    }
    return EV_OK;
}

static ev_result_t ev_oled_actor_handle_display_text(ev_oled_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    ev_oled_display_text_cmd_t cmd = {0};
    const void *payload;
    size_t payload_size;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    payload = ev_msg_payload_data(msg);
    payload_size = ev_msg_payload_size(msg);
    if ((payload == NULL) || (payload_size != sizeof(cmd))) {
        return EV_ERR_CONTRACT;
    }

    if (!ctx->pending_flush) {
        memcpy(ctx->staging_framebuffer, ctx->framebuffer, sizeof(ctx->framebuffer));
    }

    memcpy(&cmd, payload, sizeof(cmd));
    cmd.text[EV_OLED_TEXT_MAX_CHARS - 1U] = '\0';

    ++ctx->stats.display_commands_seen;
    if (ev_oled_actor_render_text_into(ctx->staging_framebuffer, &cmd) != EV_OK) {
        return EV_ERR_CONTRACT;
    }
    ev_oled_actor_mark_dirty(ctx, cmd.page, cmd.column, EV_OLED_WIDTH);
    return EV_OK;
}

static ev_result_t ev_oled_actor_handle_commit_frame(ev_oled_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const void *payload;
    size_t payload_size;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    payload = ev_msg_payload_data(msg);
    payload_size = ev_msg_payload_size(msg);
    if ((payload != NULL) && (payload_size > 0U)) {
        ev_oled_scene_t scene = {0};

        if (payload_size != sizeof(scene)) {
            return EV_ERR_CONTRACT;
        }
        memcpy(&scene, payload, sizeof(scene));
        scene.lines[0][EV_OLED_TEXT_MAX_CHARS - 1U] = '\0';
        scene.lines[1][EV_OLED_TEXT_MAX_CHARS - 1U] = '\0';
        scene.lines[2][EV_OLED_TEXT_MAX_CHARS - 1U] = '\0';

        if (ev_oled_actor_prepare_scene_commit(ctx, &scene) != EV_OK) {
            return EV_ERR_CONTRACT;
        }
    }

    if (!ctx->pending_flush) {
        return EV_OK;
    }
    if (ctx->state != EV_OLED_STATE_READY) {
        return EV_OK;
    }

    return ev_oled_actor_flush_pending(ctx);
}

static ev_result_t ev_oled_actor_handle_tick(ev_oled_actor_ctx_t *ctx)
{
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++ctx->tick_counter;
    ++ctx->stats.tick_events_seen;

    if (!ctx->boot_observed) {
        return EV_OK;
    }

    if (ctx->state == EV_OLED_STATE_READY) {
        if (ctx->pending_flush) {
            return ev_oled_actor_flush_pending(ctx);
        }
        return EV_OK;
    }

    if ((ctx->state == EV_OLED_STATE_WAIT_BOOT) || (ctx->state == EV_OLED_STATE_ERROR)) {
        if ((ctx->state == EV_OLED_STATE_ERROR) && ((int32_t)(ctx->tick_counter - ctx->retry_due_tick) < 0)) {
            return EV_OK;
        }
        return ev_oled_actor_try_initialize(ctx);
    }

    return EV_OK;
}

ev_result_t ev_oled_actor_init(ev_oled_actor_ctx_t *ctx,
                               ev_i2c_port_t *i2c_port,
                               ev_i2c_port_num_t port_num,
                               uint8_t device_address_7bit,
                               ev_oled_controller_t controller,
                               ev_delivery_fn_t deliver,
                               void *deliver_context)
{
    if ((ctx == NULL) || (i2c_port == NULL) || (i2c_port->write_stream == NULL) || (device_address_7bit > 0x7FU) ||
        (deliver == NULL) || (deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->i2c_port = i2c_port;
    ctx->deliver = deliver;
    ctx->deliver_context = deliver_context;
    ctx->port_num = port_num;
    ctx->device_address_7bit = device_address_7bit;
    ctx->controller = controller;
    ctx->state = EV_OLED_STATE_WAIT_BOOT;
    ctx->retry_delay_ticks = EV_OLED_RETRY_DELAY_TICKS;
    ctx->last_i2c_status = EV_I2C_OK;
    ctx->stats.last_i2c_status = EV_I2C_OK;
    memcpy(ctx->staging_framebuffer, ctx->framebuffer, sizeof(ctx->framebuffer));
    ev_oled_actor_reset_dirty(ctx);
    return EV_OK;
}

ev_result_t ev_oled_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_oled_actor_ctx_t *ctx = (ev_oled_actor_ctx_t *)actor_context;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_BOOT_COMPLETED:
        ++ctx->stats.boot_events_seen;
        ctx->boot_observed = true;
        return ev_oled_actor_try_initialize(ctx);

    case EV_TICK_1S:
        return ev_oled_actor_handle_tick(ctx);

    case EV_OLED_DISPLAY_TEXT_CMD:
        return ev_oled_actor_handle_display_text(ctx, msg);

    case EV_OLED_COMMIT_FRAME:
        return ev_oled_actor_handle_commit_frame(ctx, msg);

    default:
        return EV_ERR_CONTRACT;
    }
}

const ev_oled_actor_stats_t *ev_oled_actor_stats(const ev_oled_actor_ctx_t *ctx)
{
    return (ctx != NULL) ? &ctx->stats : NULL;
}
