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

static AT_CoreFinalCode_T AtCore_ParseFinalCode(const char *line, uint16_t length)
{
    if (AtCore_LineEquals(line, length, "OK") != 0U)
    {
        return AT_CORE_FINAL_OK;
    }
    if ((AtCore_LineEquals(line, length, "CONNECT") != 0U) ||
        (AtCore_LineStartsWith(line, length, "CONNECT ") != 0U))
    {
        return AT_CORE_FINAL_CONNECT;
    }
    if (AtCore_LineEquals(line, length, "ERROR") != 0U)
    {
        return AT_CORE_FINAL_ERROR;
    }
    if (AtCore_LineStartsWith(line, length, "+CME ERROR") != 0U)
    {
        return AT_CORE_FINAL_CME_ERROR;
    }
    if (AtCore_LineStartsWith(line, length, "+CMS ERROR") != 0U)
    {
        return AT_CORE_FINAL_CMS_ERROR;
    }
    if (AtCore_LineEquals(line, length, "NO CARRIER") != 0U)
    {
        return AT_CORE_FINAL_NO_CARRIER;
    }
    if (AtCore_LineEquals(line, length, "NO DIALTONE") != 0U)
    {
        return AT_CORE_FINAL_NO_DIALTONE;
    }
    if (AtCore_LineEquals(line, length, "BUSY") != 0U)
    {
        return AT_CORE_FINAL_BUSY;
    }
    if (AtCore_LineEquals(line, length, "NO ANSWER") != 0U)
    {
        return AT_CORE_FINAL_NO_ANSWER;
    }
    return AT_CORE_FINAL_NONE;
}

static uint8_t AtCore_IsFailureFinalCode(AT_CoreFinalCode_T code)
{
    return ((code == AT_CORE_FINAL_ERROR) ||
            (code == AT_CORE_FINAL_CME_ERROR) ||
            (code == AT_CORE_FINAL_CMS_ERROR) ||
            (code == AT_CORE_FINAL_NO_CARRIER) ||
            (code == AT_CORE_FINAL_NO_DIALTONE) ||
            (code == AT_CORE_FINAL_BUSY) ||
            (code == AT_CORE_FINAL_NO_ANSWER)) ? 1U : 0U;
}

static void AtCore_Complete(AT_Core_T *core,
                            AT_CoreResult_T result,
                            AT_CoreFinalCode_T final_code)
{
    core->busy = 0U;
    core->phase = AT_CORE_PHASE_IDLE;
    core->completed_result = result;
    if (final_code != AT_CORE_FINAL_NONE)
    {
        core->last_final_code = final_code;
    }

    if ((result == AT_CORE_RESULT_OK) ||
        (result == AT_CORE_RESULT_PROMPT) ||
        (result == AT_CORE_RESULT_ASYNC_EVENT))
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

    if (result == AT_CORE_RESULT_PROMPT)
    {
        core->stats.prompts++;
    }
    if (result == AT_CORE_RESULT_ASYNC_EVENT)
    {
        core->stats.async_completed++;
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

static uint8_t AtCore_IsAsyncEvent(AT_Core_T *core, const char *line, uint16_t length)
{
    if ((core->transaction_type != AT_CORE_TRANSACTION_ASYNC_OK) ||
        (core->async_match == 0))
    {
        return 0U;
    }
    return core->async_match(line, length, core->user);
}

static void AtCore_HandleAsyncEvent(AT_Core_T *core, const char *line, uint16_t length)
{
    /* Async execution reports are URC-shaped and remain visible to the driver. */
    AtCore_HandleUrc(core, line, length);

    if (core->phase == AT_CORE_PHASE_WAIT_ASYNC)
    {
        AtCore_Complete(core, AT_CORE_RESULT_ASYNC_EVENT, AT_CORE_FINAL_NONE);
    }
    else
    {
        /* Be tolerant of a fast module reporting the operation result before OK. */
        core->async_event_seen = 1U;
    }
}

static uint8_t AtCore_HandleFinalCode(AT_Core_T *core, AT_CoreFinalCode_T code)
{
    if (code == AT_CORE_FINAL_NONE)
    {
        return 0U;
    }

    core->last_final_code = code;

    if (AtCore_IsFailureFinalCode(code) != 0U)
    {
        AtCore_Complete(core, AT_CORE_RESULT_ERROR, code);
        return 1U;
    }

    /* A legacy vendor-specific success token must not be pre-empted by OK. */
    if (core->legacy_custom_result != 0U)
    {
        return 0U;
    }

    if (core->transaction_type == AT_CORE_TRANSACTION_SYNC_OK)
    {
        if (code == AT_CORE_FINAL_OK)
        {
            AtCore_Complete(core, AT_CORE_RESULT_OK, code);
            return 1U;
        }
        return 0U;
    }

    if (core->transaction_type == AT_CORE_TRANSACTION_SYNC_CONNECT)
    {
        if (code == AT_CORE_FINAL_CONNECT)
        {
            AtCore_Complete(core, AT_CORE_RESULT_OK, code);
            return 1U;
        }
        return 0U;
    }

    if (core->transaction_type == AT_CORE_TRANSACTION_ASYNC_OK)
    {
        if (code == AT_CORE_FINAL_OK)
        {
            core->stats.async_accepted++;
            if (core->async_event_seen != 0U)
            {
                AtCore_Complete(core, AT_CORE_RESULT_ASYNC_EVENT, AT_CORE_FINAL_NONE);
            }
            else
            {
                core->phase = AT_CORE_PHASE_WAIT_ASYNC;
                core->phase_start_ms = core->now_ms(core->user);
            }
            return 1U;
        }
        return 0U;
    }

    return 0U;
}

static void AtCore_HandleLine(AT_Core_T *core)
{
    uint16_t length;
    uint8_t is_urc;
    AT_CoreFinalCode_T final_code;

    length = core->line_length;
    core->line[length] = '\0';
    core->line_length = 0U;

    if (length == 0U)
    {
        return;
    }

    core->stats.lines++;

    /* A line that started while idle is unsolicited even if a command starts
     * before its trailing CR/LF arrives. */
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

    /* RING is a standard unsolicited result code, not a command final code. */
    if (AtCore_LineEquals(core->line, length, "RING") != 0U)
    {
        AtCore_HandleUrc(core, core->line, length);
        return;
    }

    /* During an async operation, its documented execution-result event wins
     * over the generic URC classifier.  Other URCs remain unsolicited. */
    if (AtCore_IsAsyncEvent(core, core->line, length) != 0U)
    {
        AtCore_HandleAsyncEvent(core, core->line, length);
        return;
    }

    final_code = AtCore_ParseFinalCode(core->line, length);
    if (AtCore_HandleFinalCode(core, final_code) != 0U)
    {
        return;
    }

    /* Legacy API may use a vendor-specific success token. */
    if ((core->legacy_custom_result != 0U) &&
        (AtCore_LineEquals(core->line, length, core->custom_success_token) != 0U))
    {
        AtCore_Complete(core, AT_CORE_RESULT_OK, AT_CORE_FINAL_CUSTOM);
        return;
    }

    /* Once an asynchronous command has been accepted by OK, subsequent lines
     * are event/URC traffic, not synchronous command response lines. */
    if (core->phase == AT_CORE_PHASE_WAIT_ASYNC)
    {
        AtCore_HandleUrc(core, core->line, length);
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
    core->phase = AT_CORE_PHASE_IDLE;
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

static AT_CoreStartResult_T AtCore_Begin(AT_Core_T *core,
                                         const char *command,
                                         const char *response_prefix,
                                         AT_CoreTransactionType_T type,
                                         uint32_t response_timeout_ms,
                                         uint32_t operation_timeout_ms,
                                         AT_CoreMatchFn async_match)
{
    uint16_t command_length;
    uint16_t sent;

    if ((core == 0) || (command == 0) || (core->tx == 0) ||
        (core->now_ms == 0) || (response_timeout_ms == 0U))
    {
        return AT_CORE_START_INVALID;
    }
    if ((type > AT_CORE_TRANSACTION_ASYNC_OK) ||
        ((type == AT_CORE_TRANSACTION_ASYNC_OK) &&
         ((operation_timeout_ms == 0U) || (async_match == 0))))
    {
        return AT_CORE_START_INVALID;
    }
    if ((core->busy != 0U) || (core->completed_result != AT_CORE_RESULT_NONE))
    {
        return AT_CORE_START_BUSY;
    }

    if ((AtCore_CopyString(core->command, AT_CORE_COMMAND_MAX, command) == 0U) ||
        (AtCore_CopyString(core->response_prefix, AT_CORE_PREFIX_MAX, response_prefix) == 0U))
    {
        return AT_CORE_START_INVALID;
    }

    core->custom_success_token[0] = '\0';
    core->legacy_custom_result = 0U;
    core->completed_result = AT_CORE_RESULT_NONE;
    core->last_final_code = AT_CORE_FINAL_NONE;
    core->response_timeout_ms = response_timeout_ms;
    core->operation_timeout_ms = operation_timeout_ms;
    core->async_match = async_match;
    core->transaction_type = type;
    core->phase = AT_CORE_PHASE_WAIT_RESPONSE;
    core->phase_start_ms = core->now_ms(core->user);
    core->async_event_seen = 0U;
    core->busy = 1U;

    command_length = AtCore_StringLength(core->command, AT_CORE_COMMAND_MAX);
    sent = core->tx((const uint8_t *)core->command, command_length, core->user);
    if (sent != command_length)
    {
        AtCore_Complete(core, AT_CORE_RESULT_TX_ERROR, AT_CORE_FINAL_NONE);
        return AT_CORE_START_TX_ERROR;
    }

    sent = core->tx((const uint8_t *)"\r\n", 2U, core->user);
    if (sent != 2U)
    {
        AtCore_Complete(core, AT_CORE_RESULT_TX_ERROR, AT_CORE_FINAL_NONE);
        return AT_CORE_START_TX_ERROR;
    }

    core->stats.commands_started++;
    return AT_CORE_START_OK;
}

AT_CoreStartResult_T AT_Core_StartTransaction(AT_Core_T *core,
                                              const AT_CoreTransaction_T *transaction)
{
    if (transaction == 0)
    {
        return AT_CORE_START_INVALID;
    }

    return AtCore_Begin(core,
                        transaction->command,
                        transaction->response_prefix,
                        transaction->type,
                        transaction->response_timeout_ms,
                        transaction->operation_timeout_ms,
                        transaction->async_match);
}

AT_CoreStartResult_T AT_Core_StartCommand(AT_Core_T *core, const AT_CoreCommand_T *command)
{
    const char *success_token;
    AT_CoreStartResult_T start_result;
    AT_CoreTransactionType_T type;

    if ((command == 0) || (command->command == 0) || (command->timeout_ms == 0U))
    {
        return AT_CORE_START_INVALID;
    }

    success_token = command->success_token;
    if ((success_token == 0) || (success_token[0] == '\0') ||
        ((success_token[0] == 'O') && (success_token[1] == 'K') &&
         (success_token[2] == '\0')))
    {
        type = AT_CORE_TRANSACTION_SYNC_OK;
    }
    else if ((success_token[0] == 'C') &&
             (AtCore_LineEquals(success_token,
                                AtCore_StringLength(success_token, AT_CORE_PREFIX_MAX),
                                "CONNECT") != 0U))
    {
        type = AT_CORE_TRANSACTION_SYNC_CONNECT;
    }
    else if ((success_token[0] == '>') && (success_token[1] == '\0'))
    {
        type = AT_CORE_TRANSACTION_PROMPT;
    }
    else
    {
        type = AT_CORE_TRANSACTION_SYNC_OK;
    }

    start_result = AtCore_Begin(core,
                                command->command,
                                command->response_prefix,
                                type,
                                command->timeout_ms,
                                0U,
                                0);
    if (start_result != AT_CORE_START_OK)
    {
        return start_result;
    }

    /* Preserve the Phase-3D legacy API semantics: a custom success token,
     * including '>', is reported as AT_CORE_RESULT_OK. */
    if ((type == AT_CORE_TRANSACTION_PROMPT) ||
        ((success_token != 0) && (success_token[0] != '\0') &&
         (AtCore_LineEquals(success_token,
                            AtCore_StringLength(success_token, AT_CORE_PREFIX_MAX),
                            "OK") == 0U) &&
         (AtCore_LineEquals(success_token,
                            AtCore_StringLength(success_token, AT_CORE_PREFIX_MAX),
                            "CONNECT") == 0U) &&
         !((success_token[0] == '>') && (success_token[1] == '\0'))))
    {
        if (AtCore_CopyString(core->custom_success_token,
                              AT_CORE_PREFIX_MAX,
                              success_token) == 0U)
        {
            AtCore_Complete(core, AT_CORE_RESULT_ERROR, AT_CORE_FINAL_NONE);
            return AT_CORE_START_INVALID;
        }
        core->legacy_custom_result = 1U;
    }

    return start_result;
}

uint16_t AT_Core_WriteRaw(AT_Core_T *core, const uint8_t *data, uint16_t length)
{
    if ((core == 0) || (core->tx == 0) || (data == 0) || (length == 0U))
    {
        return 0U;
    }
    return core->tx(data, length, core->user);
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

        /* Prompt is a transaction phase marker and may arrive without CR/LF. */
        if ((core->busy != 0U) &&
            (core->phase == AT_CORE_PHASE_WAIT_RESPONSE) &&
            (core->transaction_type == AT_CORE_TRANSACTION_PROMPT) &&
            (core->line_started_busy != 0U) &&
            (core->line_length == 1U) && (core->line[0] == '>'))
        {
            core->line_length = 0U;
            if (core->legacy_custom_result != 0U)
            {
                AtCore_Complete(core, AT_CORE_RESULT_OK, AT_CORE_FINAL_PROMPT);
            }
            else
            {
                AtCore_Complete(core, AT_CORE_RESULT_PROMPT, AT_CORE_FINAL_PROMPT);
            }
        }
    }
}

void AT_Core_Process(AT_Core_T *core)
{
    uint32_t timeout_ms;

    if ((core == 0) || (core->busy == 0U) || (core->now_ms == 0))
    {
        return;
    }

    if (core->phase == AT_CORE_PHASE_WAIT_ASYNC)
    {
        timeout_ms = core->operation_timeout_ms;
    }
    else
    {
        timeout_ms = core->response_timeout_ms;
    }

    if ((uint32_t)(core->now_ms(core->user) - core->phase_start_ms) >= timeout_ms)
    {
        AtCore_Complete(core, AT_CORE_RESULT_TIMEOUT, AT_CORE_FINAL_NONE);
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

AT_CorePhase_T AT_Core_GetPhase(const AT_Core_T *core)
{
    if (core == 0)
    {
        return AT_CORE_PHASE_IDLE;
    }
    return core->phase;
}

AT_CoreFinalCode_T AT_Core_GetLastFinalCode(const AT_Core_T *core)
{
    if (core == 0)
    {
        return AT_CORE_FINAL_NONE;
    }
    return core->last_final_code;
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
