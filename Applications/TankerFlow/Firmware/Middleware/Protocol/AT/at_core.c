#include "at_core.h"

static void AtCore_ClearBytes(void *dst, uint32_t length)
{
    uint8_t *bytes;
    uint32_t index;

    bytes = (uint8_t *)dst;
    for (index = 0U; index < length; index++)
    {
        bytes[index] = 0U;
    }
}

static void AtCore_CopyBytes(void *dst, const void *src, uint16_t length)
{
    uint8_t *dst_bytes;
    const uint8_t *src_bytes;
    uint16_t index;

    dst_bytes = (uint8_t *)dst;
    src_bytes = (const uint8_t *)src;
    for (index = 0U; index < length; index++)
    {
        dst_bytes[index] = src_bytes[index];
    }
}

static uint8_t AtCore_BytesEqual(const void *lhs, const void *rhs, uint16_t length)
{
    const uint8_t *left;
    const uint8_t *right;
    uint16_t index;

    left = (const uint8_t *)lhs;
    right = (const uint8_t *)rhs;
    for (index = 0U; index < length; index++)
    {
        if (left[index] != right[index])
        {
            return 0U;
        }
    }
    return 1U;
}

static uint16_t AtCore_StringLength(const char *text, uint16_t limit)
{
    uint16_t length;

    if (text == 0)
    {
        return 0U;
    }

    length = 0U;
    while ((length < limit) && (text[length] != '\0'))
    {
        length++;
    }
    return length;
}

static uint8_t AtCore_CopyString(char *dst, uint16_t capacity, const char *src)
{
    uint16_t length;

    if ((dst == 0) || (capacity == 0U))
    {
        return 0U;
    }

    dst[0] = '\0';
    if (src == 0)
    {
        return 1U;
    }

    length = AtCore_StringLength(src, capacity);
    if (length >= capacity)
    {
        return 0U;
    }

    if (length > 0U)
    {
        AtCore_CopyBytes(dst, src, length);
    }
    dst[length] = '\0';
    return 1U;
}

static uint8_t AtCore_LineEquals(const char *line, uint16_t length, const char *text)
{
    uint16_t text_length;

    text_length = AtCore_StringLength(text, 0xFFFFU);
    if (length != text_length)
    {
        return 0U;
    }
    if (length == 0U)
    {
        return 1U;
    }
    return AtCore_BytesEqual(line, text, length);
}

static uint8_t AtCore_LineStartsWith(const char *line, uint16_t length, const char *prefix)
{
    uint16_t prefix_length;

    prefix_length = AtCore_StringLength(prefix, 0xFFFFU);
    if (prefix_length == 0U)
    {
        return 0U;
    }
    if (length < prefix_length)
    {
        return 0U;
    }
    return AtCore_BytesEqual(line, prefix, prefix_length);
}

static uint8_t AtCore_IsErrorLine(const char *line, uint16_t length)
{
    if (AtCore_LineEquals(line, length, "ERROR") != 0U)
    {
        return 1U;
    }
    if (AtCore_LineStartsWith(line, length, "+CME ERROR") != 0U)
    {
        return 1U;
    }
    if (AtCore_LineStartsWith(line, length, "+CMS ERROR") != 0U)
    {
        return 1U;
    }
    if ((AtCore_LineEquals(line, length, "NO CARRIER") != 0U) ||
        (AtCore_LineEquals(line, length, "BUSY") != 0U) ||
        (AtCore_LineEquals(line, length, "NO ANSWER") != 0U) ||
        (AtCore_LineEquals(line, length, "NO DIALTONE") != 0U))
    {
        return 1U;
    }
    return 0U;
}

static void AtCore_Complete(AT_Core_T *core, AT_CoreResult_T result)
{
    core->busy = 0U;
    core->completed_result = result;

    if (result == AT_CORE_RESULT_OK)
    {
        core->stats.commands_ok++;
    }
    else if (result == AT_CORE_RESULT_ERROR)
    {
        core->stats.commands_error++;
    }
    else if (result == AT_CORE_RESULT_TIMEOUT)
    {
        core->stats.commands_timeout++;
    }
    else if (result == AT_CORE_RESULT_TX_ERROR)
    {
        core->stats.tx_errors++;
    }
    else
    {
    }
}

static void AtCore_HandleUrc(AT_Core_T *core, const char *line, uint16_t length)
{
    core->stats.urc_lines++;
    if (core->on_urc != 0)
    {
        core->on_urc(line, length, core->user);
    }
}

static void AtCore_HandleResponse(AT_Core_T *core, const char *line, uint16_t length)
{
    core->stats.response_lines++;
    core->stats.response_bytes += length;
    if (core->on_response != 0)
    {
        core->on_response(line, length, core->user);
    }
}

static void AtCore_HandleLine(AT_Core_T *core)
{
    uint16_t length;
    uint8_t is_urc;

    length = core->line_length;
    core->line[length] = '\0';
    core->line_length = 0U;

    if (length == 0U)
    {
        return;
    }

    core->stats.lines++;

    if ((core->busy == 0U) || (core->line_started_busy == 0U))
    {
        AtCore_HandleUrc(core, core->line, length);
        return;
    }

    if (AtCore_LineEquals(core->line, length, core->command) != 0U)
    {
        core->stats.echo_lines++;
        return;
    }

    if (AtCore_IsErrorLine(core->line, length) != 0U)
    {
        /* Error final result codes terminate the transaction. */
        AtCore_Complete(core, AT_CORE_RESULT_ERROR);
        return;
    }

    if (AtCore_LineEquals(core->line, length, core->success_token) != 0U)
    {
        AtCore_Complete(core, AT_CORE_RESULT_OK);
        return;
    }

    if (AtCore_LineStartsWith(core->line, length, core->response_prefix) != 0U)
    {
        AtCore_HandleResponse(core, core->line, length);
        return;
    }

    is_urc = 0U;
    if (core->is_urc != 0)
    {
        is_urc = core->is_urc(core->line, length, core->user);
    }

    if (is_urc != 0U)
    {
        AtCore_HandleUrc(core, core->line, length);
    }
    else
    {
        AtCore_HandleResponse(core, core->line, length);
    }
}

void AT_Core_Init(AT_Core_T *core,
                  AT_CoreTxFn tx,
                  AT_CoreNowMsFn now_ms,
                  AT_CoreIsUrcFn is_urc,
                  AT_CoreLineFn on_response,
                  AT_CoreLineFn on_urc,
                  void *user)
{
    if (core == 0)
    {
        return;
    }

    AtCore_ClearBytes(core, (uint32_t)sizeof(*core));
    core->tx = tx;
    core->now_ms = now_ms;
    core->is_urc = is_urc;
    core->on_response = on_response;
    core->on_urc = on_urc;
    core->user = user;
    (void)AtCore_CopyString(core->success_token, AT_CORE_PREFIX_MAX, "OK");
}

void AT_Core_Reset(AT_Core_T *core)
{
    AT_CoreTxFn tx;
    AT_CoreNowMsFn now_ms;
    AT_CoreIsUrcFn is_urc;
    AT_CoreLineFn on_response;
    AT_CoreLineFn on_urc;
    void *user;

    if (core == 0)
    {
        return;
    }

    tx = core->tx;
    now_ms = core->now_ms;
    is_urc = core->is_urc;
    on_response = core->on_response;
    on_urc = core->on_urc;
    user = core->user;
    AT_Core_Init(core, tx, now_ms, is_urc, on_response, on_urc, user);
}

AT_CoreStartResult_T AT_Core_StartCommand(AT_Core_T *core, const AT_CoreCommand_T *command)
{
    const char *success_token;
    uint16_t command_length;
    uint16_t sent;

    if ((core == 0) || (command == 0) || (command->command == 0) ||
        (core->tx == 0) || (core->now_ms == 0) || (command->timeout_ms == 0U))
    {
        return AT_CORE_START_INVALID;
    }
    if ((core->busy != 0U) || (core->completed_result != AT_CORE_RESULT_NONE))
    {
        return AT_CORE_START_BUSY;
    }

    success_token = command->success_token;
    if ((success_token == 0) || (success_token[0] == '\0'))
    {
        success_token = "OK";
    }

    if ((AtCore_CopyString(core->command, AT_CORE_COMMAND_MAX, command->command) == 0U) ||
        (AtCore_CopyString(core->response_prefix, AT_CORE_PREFIX_MAX, command->response_prefix) == 0U) ||
        (AtCore_CopyString(core->success_token, AT_CORE_PREFIX_MAX, success_token) == 0U))
    {
        return AT_CORE_START_INVALID;
    }

    core->completed_result = AT_CORE_RESULT_NONE;
    core->command_timeout_ms = command->timeout_ms;
    core->command_start_ms = core->now_ms(core->user);
    core->busy = 1U;

    command_length = AtCore_StringLength(core->command, AT_CORE_COMMAND_MAX);
    sent = core->tx((const uint8_t *)core->command, command_length, core->user);
    if (sent != command_length)
    {
        AtCore_Complete(core, AT_CORE_RESULT_TX_ERROR);
        return AT_CORE_START_TX_ERROR;
    }

    sent = core->tx((const uint8_t *)"\r\n", 2U, core->user);
    if (sent != 2U)
    {
        AtCore_Complete(core, AT_CORE_RESULT_TX_ERROR);
        return AT_CORE_START_TX_ERROR;
    }

    core->stats.commands_started++;
    return AT_CORE_START_OK;
}

void AT_Core_Feed(AT_Core_T *core, const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if ((core == 0) || (data == 0) || (length == 0U))
    {
        return;
    }

    for (index = 0U; index < length; index++)
    {
        uint8_t byte;

        byte = data[index];
        core->stats.rx_bytes++;

        if (core->dropping_line != 0U)
        {
            if (byte == (uint8_t)'\n')
            {
                core->dropping_line = 0U;
                core->line_length = 0U;
            }
            continue;
        }

        if (byte == (uint8_t)'\r')
        {
            continue;
        }

        if (byte == (uint8_t)'\n')
        {
            AtCore_HandleLine(core);
            continue;
        }

        if (core->line_length == 0U)
        {
            core->line_started_busy = core->busy;
        }

        if (core->line_length >= (AT_CORE_LINE_MAX - 1U))
        {
            core->stats.line_overflow++;
            core->line_length = 0U;
            core->dropping_line = 1U;
            continue;
        }

        core->line[core->line_length++] = (char)byte;

        /* MC610-style data-send prompts can be a bare '>' without CR/LF. */
        if ((core->busy != 0U) && (core->line_started_busy != 0U) &&
            (core->line_length == 1U) && (core->success_token[0] == '>') &&
            (core->success_token[1] == '\0') && (core->line[0] == '>'))
        {
            core->line_length = 0U;
            AtCore_Complete(core, AT_CORE_RESULT_OK);
        }
    }
}

void AT_Core_Process(AT_Core_T *core)
{
    if ((core == 0) || (core->busy == 0U) || (core->now_ms == 0))
    {
        return;
    }

    if ((uint32_t)(core->now_ms(core->user) - core->command_start_ms) >= core->command_timeout_ms)
    {
        AtCore_Complete(core, AT_CORE_RESULT_TIMEOUT);
    }
}

uint8_t AT_Core_IsBusy(const AT_Core_T *core)
{
    if (core == 0)
    {
        return 0U;
    }
    return core->busy;
}

AT_CoreResult_T AT_Core_PeekResult(const AT_Core_T *core)
{
    if (core == 0)
    {
        return AT_CORE_RESULT_NONE;
    }
    return core->completed_result;
}

AT_CoreResult_T AT_Core_TakeResult(AT_Core_T *core)
{
    AT_CoreResult_T result;

    if (core == 0)
    {
        return AT_CORE_RESULT_NONE;
    }

    result = core->completed_result;
    if (result != AT_CORE_RESULT_NONE)
    {
        core->completed_result = AT_CORE_RESULT_NONE;
    }
    return result;
}

const AT_CoreStats_T *AT_Core_GetStats(const AT_Core_T *core)
{
    if (core == 0)
    {
        return 0;
    }
    return &core->stats;
}
