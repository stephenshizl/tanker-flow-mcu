#include "modem_4g.h"

#include "platform_port.h"

#define MODEM4G_READ_CHUNK_SIZE         (128U)
#define MODEM4G_LAST_URC_MAX            (128U)

/* MC610 hardware-guide minimums plus small scheduling margin. */
#define MODEM4G_VBAT_SETTLE_MS          (30U)
#define MODEM4G_PWRKEY_ON_HOLD_MS       (2000U)
#define MODEM4G_RESET_HOLD_MS           (100U)

/* Software policy. These are not MC610 electrical timing requirements. */
#define MODEM4G_AT_TIMEOUT_MS            (1000U)
#define MODEM4G_BOOT_SYNC_WINDOW_MS      (30000U)
#define MODEM4G_RETRY_GAP_MS             (500U)
#define MODEM4G_SIM_POLL_MS              (3000U)
#define MODEM4G_REG_POLL_MS              (2000U)
#define MODEM4G_COMMAND_RETRY_MAX        (3U)
#define MODEM4G_RESET_ATTEMPT_MAX        (3U)

#define MODEM4G_REG_INVALID              (0xFFU)
#define MODEM4G_CSQ_UNKNOWN              (99U)

typedef enum
{
    MODEM4G_STEP_WAIT = 0,
    MODEM4G_STEP_OK,
    MODEM4G_STEP_ERROR,
    MODEM4G_STEP_TIMEOUT,
    MODEM4G_STEP_TX_ERROR
} Modem4G_CommandStep_T;

typedef struct
{
    uint8_t cpin_seen;
    uint8_t cpin_ready;
    uint8_t csq_seen;
    uint8_t csq;
    uint8_t cereg_seen;
    uint8_t cereg;
    uint8_t cgreg_seen;
    uint8_t cgreg;
    uint8_t creg_seen;
    uint8_t creg;
} Modem4G_CommandData_T;

typedef void (*Modem4G_UrcHandlerFn)(const char *line, uint16_t length);

typedef struct
{
    const char *prefix;
    Modem4G_UrcHandlerFn handler;
} Modem4G_UrcRoute_T;

static void Modem4G_HandleNetworkUrc(const char *line, uint16_t length);
static void Modem4G_HandleStartupUrc(const char *line, uint16_t length);
static void Modem4G_HandleMipTcpRxUrc(const char *line, uint16_t length);
static void Modem4G_HandleMipUdpRxUrc(const char *line, uint16_t length);
static void Modem4G_HandleMipStatUrc(const char *line, uint16_t length);
static void Modem4G_HandleTcpEventUrc(const char *line, uint16_t length);

static const Modem4G_UrcRoute_T g_urc_routes[] =
{
    { "+CEREG:", Modem4G_HandleNetworkUrc },
    { "+CGREG:", Modem4G_HandleNetworkUrc },
    { "+CREG:", Modem4G_HandleNetworkUrc },
    { "AT READY", Modem4G_HandleStartupUrc },
    { "+SIM READY", Modem4G_HandleStartupUrc },
    { "RDY", Modem4G_HandleStartupUrc },
    { "PB DONE", Modem4G_HandleStartupUrc },
    { "SMS Ready", Modem4G_HandleStartupUrc },
    { "Call Ready", Modem4G_HandleStartupUrc },
    { "+MIPRTCP:", Modem4G_HandleMipTcpRxUrc },
    { "+MIPRUDP:", Modem4G_HandleMipUdpRxUrc },
    { "+MIPSTAT:", Modem4G_HandleMipStatUrc },
    { "+MIPCALL:", Modem4G_HandleTcpEventUrc },
    { "+MIPOPEN:", Modem4G_HandleTcpEventUrc },
    { "+MIPCLOSE:", Modem4G_HandleTcpEventUrc },
    { "+MIPNTP:", Modem4G_HandleTcpEventUrc },
    { "+MPING:", Modem4G_HandleTcpEventUrc },
    { "+MPINGSTAT:", Modem4G_HandleTcpEventUrc },
    { "+MIPXOFF", Modem4G_HandleTcpEventUrc },
    { "+MIPXON", Modem4G_HandleTcpEventUrc },
    { "+MIPDATA:", Modem4G_HandleTcpEventUrc },
    { "+MIPPUSH:", Modem4G_HandleTcpEventUrc },
    { "+MIPSEND:", Modem4G_HandleTcpEventUrc }
};

#define MODEM4G_URC_ROUTE_COUNT \
    ((uint16_t)(sizeof(g_urc_routes) / sizeof(g_urc_routes[0])))

static uint16_t Modem4G_StringLength(const char *text)
{
    uint16_t length;

    length = 0U;
    while (text[length] != '\0')
    {
        length++;
    }
    return length;
}

static void Modem4G_ClearBytes(void *dst, uint16_t length)
{
    uint8_t *bytes;
    uint16_t index;

    bytes = (uint8_t *)dst;
    for (index = 0U; index < length; index++)
    {
        bytes[index] = 0U;
    }
}

static void Modem4G_CopyBytes(void *dst, const void *src, uint16_t length)
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

static AT_Core_T g_at_core;
static Modem4G_Stats_T g_modem_stats;
static Modem4G_Status_T g_status;
static char g_last_urc[MODEM4G_LAST_URC_MAX];
static uint32_t g_state_start_ms;
static uint32_t g_boot_sync_start_ms;
static uint32_t g_retry_start_ms;
static uint8_t g_command_issued;
static uint8_t g_command_retry_count;
static uint8_t g_retry_waiting;
static Modem4G_CommandData_T g_command_data;

static uint8_t Modem4G_StartsWith(const char *line, uint16_t length, const char *prefix)
{
    uint16_t prefix_length;
    uint16_t index;

    prefix_length = Modem4G_StringLength(prefix);
    if (length < prefix_length)
    {
        return 0U;
    }
    for (index = 0U; index < prefix_length; index++)
    {
        if (line[index] != prefix[index])
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t Modem4G_StringStartsWith(const char *text, const char *prefix)
{
    return Modem4G_StartsWith(text, Modem4G_StringLength(text), prefix);
}

static uint8_t Modem4G_IsElapsed(uint32_t start_ms, uint32_t period_ms)
{
    return ((uint32_t)(PlatformPort_GetMs() - start_ms) >= period_ms) ? 1U : 0U;
}

static uint16_t Modem4G_Tx(const uint8_t *data, uint16_t length, void *user)
{
    (void)user;
    return PlatformPort_ModemWrite(data, length);
}

static uint32_t Modem4G_NowMs(void *user)
{
    (void)user;
    return PlatformPort_GetMs();
}

static void Modem4G_SetState(Modem4G_State_T state)
{
    if (g_status.state != state)
    {
        g_status.state = state;
        g_state_start_ms = PlatformPort_GetMs();
        g_command_issued = 0U;
        g_command_retry_count = 0U;
        g_retry_waiting = 0U;
        g_modem_stats.state_transitions++;
        if (state == MODEM4G_STATE_READY)
        {
            g_status.reset_attempts = 0U;
            g_modem_stats.ready_count++;
        }
    }
}

static uint8_t Modem4G_IsRegisteredValue(uint8_t value)
{
    return ((value == (uint8_t)MODEM4G_REG_HOME) ||
            (value == (uint8_t)MODEM4G_REG_ROAMING)) ? 1U : 0U;
}

static void Modem4G_UpdateRegistered(void)
{
    g_status.registered =
        ((Modem4G_IsRegisteredValue(g_status.cereg) != 0U) ||
         (Modem4G_IsRegisteredValue(g_status.cgreg) != 0U) ||
         (Modem4G_IsRegisteredValue(g_status.creg) != 0U)) ? 1U : 0U;
}

static uint8_t Modem4G_ParseUnsigned(const char **cursor, uint8_t *value)
{
    const char *ptr;
    uint16_t number;
    uint8_t digits;

    if ((cursor == 0) || (*cursor == 0) || (value == 0))
    {
        return 0U;
    }

    ptr = *cursor;
    while ((*ptr == ' ') || (*ptr == '\t'))
    {
        ptr++;
    }

    number = 0U;
    digits = 0U;
    while ((*ptr >= '0') && (*ptr <= '9'))
    {
        number = (uint16_t)((number * 10U) + (uint16_t)(*ptr - '0'));
        if (number > 255U)
        {
            return 0U;
        }
        digits++;
        ptr++;
    }

    if (digits == 0U)
    {
        return 0U;
    }

    *value = (uint8_t)number;
    *cursor = ptr;
    return 1U;
}

static uint8_t Modem4G_ParseRegistration(const char *line, uint8_t *status)
{
    const char *cursor;
    uint8_t first;
    uint8_t second;

    if ((line == 0) || (status == 0))
    {
        return 0U;
    }

    cursor = line;
    while ((*cursor != '\0') && (*cursor != ':'))
    {
        cursor++;
    }
    if (*cursor != ':')
    {
        return 0U;
    }
    cursor++;

    if (Modem4G_ParseUnsigned(&cursor, &first) == 0U)
    {
        return 0U;
    }

    while ((*cursor == ' ') || (*cursor == '\t'))
    {
        cursor++;
    }
    if (*cursor == ',')
    {
        cursor++;
        if (Modem4G_ParseUnsigned(&cursor, &second) != 0U)
        {
            *status = second;
            return 1U;
        }
    }

    *status = first;
    return 1U;
}

static uint8_t Modem4G_ParseCsq(const char *line, uint8_t *csq)
{
    const char *cursor;
    uint8_t value;

    if ((line == 0) || (csq == 0) || (Modem4G_StringStartsWith(line, "+CSQ:") == 0U))
    {
        return 0U;
    }

    cursor = &line[5];
    if (Modem4G_ParseUnsigned(&cursor, &value) == 0U)
    {
        return 0U;
    }
    *csq = value;
    return 1U;
}

static uint8_t Modem4G_IsCpinReady(const char *line)
{
    const char *cursor;

    if ((line == 0) || (Modem4G_StringStartsWith(line, "+CPIN:") == 0U))
    {
        return 0U;
    }

    cursor = &line[6];
    while ((*cursor == ' ') || (*cursor == '\t'))
    {
        cursor++;
    }
    return ((cursor[0] == 'R') && (cursor[1] == 'E') && (cursor[2] == 'A') &&
            (cursor[3] == 'D') && (cursor[4] == 'Y') && (cursor[5] == '\0')) ? 1U : 0U;
}

static const Modem4G_UrcRoute_T *Modem4G_FindUrcRoute(const char *line, uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < MODEM4G_URC_ROUTE_COUNT; index++)
    {
        if (Modem4G_StartsWith(line, length, g_urc_routes[index].prefix) != 0U)
        {
            return &g_urc_routes[index];
        }
    }
    return 0;
}

static uint8_t Modem4G_IsKnownUrc(const char *line, uint16_t length, void *user)
{
    (void)user;
    return (Modem4G_FindUrcRoute(line, length) != 0) ? 1U : 0U;
}

static void Modem4G_OnResponse(const char *line, uint16_t length, void *user)
{
    uint8_t value;

    (void)user;

    /*
     * Parse response lines as they arrive.  Do not accumulate the whole AT
     * response: a command may contain many lines or more than 1 KB in total.
     */
    if (Modem4G_StartsWith(line, length, "+CPIN:") != 0U)
    {
        g_command_data.cpin_seen = 1U;
        g_command_data.cpin_ready = Modem4G_IsCpinReady(line);
    }
    else if (Modem4G_StartsWith(line, length, "+CSQ:") != 0U)
    {
        g_command_data.csq_seen = 1U;
        if (Modem4G_ParseCsq(line, &value) != 0U)
        {
            g_command_data.csq = value;
        }
        else
        {
            g_command_data.csq = MODEM4G_CSQ_UNKNOWN;
        }
    }
    else if (Modem4G_StartsWith(line, length, "+CEREG:") != 0U)
    {
        if (Modem4G_ParseRegistration(line, &value) != 0U)
        {
            g_command_data.cereg_seen = 1U;
            g_command_data.cereg = value;
        }
    }
    else if (Modem4G_StartsWith(line, length, "+CGREG:") != 0U)
    {
        if (Modem4G_ParseRegistration(line, &value) != 0U)
        {
            g_command_data.cgreg_seen = 1U;
            g_command_data.cgreg = value;
        }
    }
    else if (Modem4G_StartsWith(line, length, "+CREG:") != 0U)
    {
        if (Modem4G_ParseRegistration(line, &value) != 0U)
        {
            g_command_data.creg_seen = 1U;
            g_command_data.creg = value;
        }
    }
    else
    {
    }
}

static void Modem4G_HandleNetworkUrc(const char *line, uint16_t length)
{
    uint8_t reg_status;

    g_modem_stats.network_urc_count++;
    if (Modem4G_ParseRegistration(line, &reg_status) == 0U)
    {
        return;
    }

    if (Modem4G_StartsWith(line, length, "+CEREG:") != 0U)
    {
        g_status.cereg = reg_status;
    }
    else if (Modem4G_StartsWith(line, length, "+CGREG:") != 0U)
    {
        g_status.cgreg = reg_status;
    }
    else if (Modem4G_StartsWith(line, length, "+CREG:") != 0U)
    {
        g_status.creg = reg_status;
    }
    else
    {
        return;
    }

    Modem4G_UpdateRegistered();
}

static void Modem4G_HandleStartupUrc(const char *line, uint16_t length)
{
    g_modem_stats.startup_urc_count++;
    g_status.alive = 1U;

    if (Modem4G_StartsWith(line, length, "+SIM READY") != 0U)
    {
        g_status.sim_ready = 1U;
    }
}

static void Modem4G_HandleMipTcpRxUrc(const char *line, uint16_t length)
{
    (void)line;
    (void)length;
    g_modem_stats.miprtcp_count++;
}

static void Modem4G_HandleMipUdpRxUrc(const char *line, uint16_t length)
{
    (void)line;
    (void)length;
    g_modem_stats.miprudp_count++;
}

static void Modem4G_HandleMipStatUrc(const char *line, uint16_t length)
{
    (void)line;
    (void)length;
    g_modem_stats.mipstat_count++;
    g_modem_stats.tcp_event_urc_count++;
}

static void Modem4G_HandleTcpEventUrc(const char *line, uint16_t length)
{
    (void)line;
    (void)length;
    g_modem_stats.tcp_event_urc_count++;
}

static void Modem4G_OnUrc(const char *line, uint16_t length, void *user)
{
    const Modem4G_UrcRoute_T *route;
    uint16_t copy_length;

    (void)user;
    g_modem_stats.urc_count++;

    route = Modem4G_FindUrcRoute(line, length);
    if ((route != 0) && (route->handler != 0))
    {
        route->handler(line, length);
    }
    else
    {
        g_modem_stats.unknown_urc_count++;
    }

    copy_length = length;
    if (copy_length >= MODEM4G_LAST_URC_MAX)
    {
        copy_length = MODEM4G_LAST_URC_MAX - 1U;
    }
    if (copy_length > 0U)
    {
        Modem4G_CopyBytes(g_last_urc, line, copy_length);
    }
    g_last_urc[copy_length] = '\0';
}

static void Modem4G_ResetCommandData(void)
{
    Modem4G_ClearBytes(&g_command_data, (uint16_t)sizeof(g_command_data));
    g_command_data.csq = MODEM4G_CSQ_UNKNOWN;
    g_command_data.cereg = MODEM4G_REG_INVALID;
    g_command_data.cgreg = MODEM4G_REG_INVALID;
    g_command_data.creg = MODEM4G_REG_INVALID;
}

static void Modem4G_ResetCommandTracking(void)
{
    g_command_issued = 0U;
    g_command_retry_count = 0U;
    g_retry_waiting = 0U;
    Modem4G_ResetCommandData();
}

static void Modem4G_BeginBootSyncWindow(void)
{
    g_boot_sync_start_ms = PlatformPort_GetMs();
    Modem4G_ResetCommandTracking();
    Modem4G_SetState(MODEM4G_STATE_AT_SYNC);
}

static void Modem4G_EnterHardwareReset(void)
{
    if (g_status.reset_attempts >= MODEM4G_RESET_ATTEMPT_MAX)
    {
        Modem4G_SetState(MODEM4G_STATE_ERROR);
        return;
    }

    g_status.reset_attempts++;
    g_modem_stats.reset_count++;
    AT_Core_Reset(&g_at_core);
    PlatformPort_ModemPowerKeyRelease();
    PlatformPort_ModemResetAssert();
    Modem4G_SetState(MODEM4G_STATE_RESET_ASSERT);
}

static void Modem4G_ScheduleRetry(void)
{
    g_command_issued = 0U;
    g_retry_waiting = 1U;
    g_retry_start_ms = PlatformPort_GetMs();
    g_modem_stats.command_failures++;
}

static uint8_t Modem4G_RetryReady(void)
{
    if (g_retry_waiting == 0U)
    {
        return 1U;
    }
    if (Modem4G_IsElapsed(g_retry_start_ms, MODEM4G_RETRY_GAP_MS) == 0U)
    {
        return 0U;
    }
    g_retry_waiting = 0U;
    return 1U;
}

static AT_CoreStartResult_T Modem4G_StartSyncTransaction(const char *command,
                                                           const char *response_prefix,
                                                           uint32_t timeout_ms)
{
    AT_CoreTransaction_T transaction;

    transaction.command = command;
    transaction.response_prefix = response_prefix;
    transaction.type = AT_CORE_TRANSACTION_SYNC_OK;
    transaction.response_timeout_ms = timeout_ms;
    transaction.operation_timeout_ms = 0U;
    transaction.async_match = 0;
    return AT_Core_StartTransaction(&g_at_core, &transaction);
}

static Modem4G_CommandStep_T Modem4G_CommandStep(const char *command,
                                                  const char *prefix)
{
    AT_CoreStartResult_T start_result;
    AT_CoreResult_T result;

    if (g_command_issued == 0U)
    {
        if (Modem4G_RetryReady() == 0U)
        {
            return MODEM4G_STEP_WAIT;
        }

        Modem4G_ResetCommandData();
        start_result = Modem4G_StartSyncTransaction(command, prefix, MODEM4G_AT_TIMEOUT_MS);
        if (start_result == AT_CORE_START_OK)
        {
            g_command_issued = 1U;
            return MODEM4G_STEP_WAIT;
        }
        if (start_result == AT_CORE_START_BUSY)
        {
            return MODEM4G_STEP_WAIT;
        }
        if (start_result == AT_CORE_START_TX_ERROR)
        {
            (void)AT_Core_TakeResult(&g_at_core);
            return MODEM4G_STEP_TX_ERROR;
        }
        return MODEM4G_STEP_ERROR;
    }

    if (AT_Core_IsBusy(&g_at_core) != 0U)
    {
        return MODEM4G_STEP_WAIT;
    }

    result = AT_Core_TakeResult(&g_at_core);
    g_command_issued = 0U;
    if (result == AT_CORE_RESULT_OK)
    {
        g_status.alive = 1U;
        g_status.last_ok_ms = PlatformPort_GetMs();
        return MODEM4G_STEP_OK;
    }
    if (result == AT_CORE_RESULT_TIMEOUT)
    {
        return MODEM4G_STEP_TIMEOUT;
    }
    if (result == AT_CORE_RESULT_TX_ERROR)
    {
        return MODEM4G_STEP_TX_ERROR;
    }
    return MODEM4G_STEP_ERROR;
}

static uint8_t Modem4G_HandleRetryableFailure(void)
{
    g_command_retry_count++;
    Modem4G_ScheduleRetry();
    if (g_command_retry_count >= MODEM4G_COMMAND_RETRY_MAX)
    {
        Modem4G_EnterHardwareReset();
        return 0U;
    }
    return 1U;
}

static void Modem4G_ProcessStateMachine(void)
{
    Modem4G_CommandStep_T step;

    switch (g_status.state)
    {
        case MODEM4G_STATE_OFF:
            break;

        case MODEM4G_STATE_VBAT_SETTLE:
            if (Modem4G_IsElapsed(g_state_start_ms, MODEM4G_VBAT_SETTLE_MS) != 0U)
            {
                PlatformPort_ModemResetRelease();
                PlatformPort_ModemPowerKeyAssert();
                g_modem_stats.power_on_count++;
                Modem4G_SetState(MODEM4G_STATE_PWRKEY_ASSERT);
            }
            break;

        case MODEM4G_STATE_PWRKEY_ASSERT:
            if (Modem4G_IsElapsed(g_state_start_ms, MODEM4G_PWRKEY_ON_HOLD_MS) != 0U)
            {
                PlatformPort_ModemPowerKeyRelease();
                Modem4G_BeginBootSyncWindow();
            }
            break;

        case MODEM4G_STATE_AT_SYNC:
            step = Modem4G_CommandStep("AT", 0);
            if (step == MODEM4G_STEP_OK)
            {
                Modem4G_SetState(MODEM4G_STATE_RESULT_CODES_ON);
            }
            else if ((step == MODEM4G_STEP_ERROR) || (step == MODEM4G_STEP_TIMEOUT) ||
                     (step == MODEM4G_STEP_TX_ERROR))
            {
                Modem4G_ScheduleRetry();
                if (Modem4G_IsElapsed(g_boot_sync_start_ms, MODEM4G_BOOT_SYNC_WINDOW_MS) != 0U)
                {
                    Modem4G_EnterHardwareReset();
                }
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_RESULT_CODES_ON:
            /* Keep final result codes enabled; ATQ1 would make completion ambiguous. */
            step = Modem4G_CommandStep("ATQ0", 0);
            if (step == MODEM4G_STEP_OK)
            {
                Modem4G_SetState(MODEM4G_STATE_VERBOSE_MODE);
            }
            else if ((step == MODEM4G_STEP_ERROR) || (step == MODEM4G_STEP_TIMEOUT) ||
                     (step == MODEM4G_STEP_TX_ERROR))
            {
                (void)Modem4G_HandleRetryableFailure();
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_VERBOSE_MODE:
            /* Force text final result codes such as OK/ERROR/NO CARRIER. */
            step = Modem4G_CommandStep("ATV1", 0);
            if (step == MODEM4G_STEP_OK)
            {
                Modem4G_SetState(MODEM4G_STATE_ECHO_OFF);
            }
            else if ((step == MODEM4G_STEP_ERROR) || (step == MODEM4G_STEP_TIMEOUT) ||
                     (step == MODEM4G_STEP_TX_ERROR))
            {
                (void)Modem4G_HandleRetryableFailure();
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_ECHO_OFF:
            step = Modem4G_CommandStep("ATE0", 0);
            if (step == MODEM4G_STEP_OK)
            {
                Modem4G_SetState(MODEM4G_STATE_CEREG_URC_ENABLE);
            }
            else if ((step == MODEM4G_STEP_ERROR) || (step == MODEM4G_STEP_TIMEOUT) ||
                     (step == MODEM4G_STEP_TX_ERROR))
            {
                (void)Modem4G_HandleRetryableFailure();
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_CEREG_URC_ENABLE:
            /* Best-effort enable EPS registration URCs.  Failure is non-fatal;
             * polling remains the fallback registration mechanism. */
            step = Modem4G_CommandStep("AT+CEREG=1", 0);
            if (step != MODEM4G_STEP_WAIT)
            {
                if (step != MODEM4G_STEP_OK)
                {
                    g_modem_stats.command_failures++;
                }
                Modem4G_SetState(MODEM4G_STATE_CGREG_URC_ENABLE);
            }
            break;

        case MODEM4G_STATE_CGREG_URC_ENABLE:
            /* Best-effort enable packet-domain registration URCs for fallback RATs. */
            step = Modem4G_CommandStep("AT+CGREG=1", 0);
            if (step != MODEM4G_STEP_WAIT)
            {
                if (step != MODEM4G_STEP_OK)
                {
                    g_modem_stats.command_failures++;
                }
                Modem4G_SetState(MODEM4G_STATE_CREG_URC_ENABLE);
            }
            break;

        case MODEM4G_STATE_CREG_URC_ENABLE:
            /* Best-effort enable CS-domain registration URCs. */
            step = Modem4G_CommandStep("AT+CREG=1", 0);
            if (step != MODEM4G_STEP_WAIT)
            {
                if (step != MODEM4G_STEP_OK)
                {
                    g_modem_stats.command_failures++;
                }
                Modem4G_SetState(MODEM4G_STATE_SIM_CHECK);
            }
            break;

        case MODEM4G_STATE_SIM_CHECK:
            step = Modem4G_CommandStep("AT+CPIN?", "+CPIN:");
            if (step == MODEM4G_STEP_OK)
            {
                if (g_command_data.cpin_seen == 0U)
                {
                    (void)Modem4G_HandleRetryableFailure();
                }
                else if (g_command_data.cpin_ready != 0U)
                {
                    g_status.sim_ready = 1U;
                    Modem4G_SetState(MODEM4G_STATE_SIGNAL_CHECK);
                }
                else
                {
                    g_status.sim_ready = 0U;
                    Modem4G_SetState(MODEM4G_STATE_SIM_WAIT);
                }
            }
            else if ((step == MODEM4G_STEP_ERROR) || (step == MODEM4G_STEP_TIMEOUT) ||
                     (step == MODEM4G_STEP_TX_ERROR))
            {
                (void)Modem4G_HandleRetryableFailure();
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_SIM_WAIT:
            if (Modem4G_IsElapsed(g_state_start_ms, MODEM4G_SIM_POLL_MS) != 0U)
            {
                Modem4G_SetState(MODEM4G_STATE_SIM_CHECK);
            }
            break;

        case MODEM4G_STATE_SIGNAL_CHECK:
            step = Modem4G_CommandStep("AT+CSQ", "+CSQ:");
            if (step == MODEM4G_STEP_OK)
            {
                g_status.csq = MODEM4G_CSQ_UNKNOWN;
                g_status.csq_valid = 0U;
                if (g_command_data.csq_seen != 0U)
                {
                    g_status.csq = g_command_data.csq;
                    if (g_command_data.csq <= 31U)
                    {
                        g_status.csq_valid = 1U;
                    }
                }
                Modem4G_SetState(MODEM4G_STATE_CEREG_CHECK);
            }
            else if (step == MODEM4G_STEP_ERROR)
            {
                g_status.csq = MODEM4G_CSQ_UNKNOWN;
                g_status.csq_valid = 0U;
                Modem4G_SetState(MODEM4G_STATE_CEREG_CHECK);
            }
            else if ((step == MODEM4G_STEP_TIMEOUT) || (step == MODEM4G_STEP_TX_ERROR))
            {
                (void)Modem4G_HandleRetryableFailure();
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_CEREG_CHECK:
            step = Modem4G_CommandStep("AT+CEREG?", "+CEREG:");
            if (step == MODEM4G_STEP_OK)
            {
                g_status.cereg = MODEM4G_REG_INVALID;
                if (g_command_data.cereg_seen != 0U)
                {
                    g_status.cereg = g_command_data.cereg;
                }
                Modem4G_UpdateRegistered();
                if (g_status.registered != 0U)
                {
                    Modem4G_SetState(MODEM4G_STATE_READY);
                }
                else
                {
                    Modem4G_SetState(MODEM4G_STATE_CGREG_CHECK);
                }
            }
            else if (step == MODEM4G_STEP_ERROR)
            {
                g_status.cereg = MODEM4G_REG_INVALID;
                Modem4G_UpdateRegistered();
                Modem4G_SetState(MODEM4G_STATE_CGREG_CHECK);
            }
            else if ((step == MODEM4G_STEP_TIMEOUT) || (step == MODEM4G_STEP_TX_ERROR))
            {
                (void)Modem4G_HandleRetryableFailure();
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_CGREG_CHECK:
            step = Modem4G_CommandStep("AT+CGREG?", "+CGREG:");
            if (step == MODEM4G_STEP_OK)
            {
                g_status.cgreg = MODEM4G_REG_INVALID;
                if (g_command_data.cgreg_seen != 0U)
                {
                    g_status.cgreg = g_command_data.cgreg;
                }
                Modem4G_UpdateRegistered();
                if (g_status.registered != 0U)
                {
                    Modem4G_SetState(MODEM4G_STATE_READY);
                }
                else
                {
                    Modem4G_SetState(MODEM4G_STATE_CREG_CHECK);
                }
            }
            else if (step == MODEM4G_STEP_ERROR)
            {
                g_status.cgreg = MODEM4G_REG_INVALID;
                Modem4G_UpdateRegistered();
                Modem4G_SetState(MODEM4G_STATE_CREG_CHECK);
            }
            else if ((step == MODEM4G_STEP_TIMEOUT) || (step == MODEM4G_STEP_TX_ERROR))
            {
                (void)Modem4G_HandleRetryableFailure();
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_CREG_CHECK:
            step = Modem4G_CommandStep("AT+CREG?", "+CREG:");
            if (step == MODEM4G_STEP_OK)
            {
                g_status.creg = MODEM4G_REG_INVALID;
                if (g_command_data.creg_seen != 0U)
                {
                    g_status.creg = g_command_data.creg;
                }
                Modem4G_UpdateRegistered();
                if (g_status.registered != 0U)
                {
                    Modem4G_SetState(MODEM4G_STATE_READY);
                }
                else
                {
                    Modem4G_SetState(MODEM4G_STATE_REG_WAIT);
                }
            }
            else if (step == MODEM4G_STEP_ERROR)
            {
                g_status.creg = MODEM4G_REG_INVALID;
                Modem4G_UpdateRegistered();
                Modem4G_SetState(MODEM4G_STATE_REG_WAIT);
            }
            else if ((step == MODEM4G_STEP_TIMEOUT) || (step == MODEM4G_STEP_TX_ERROR))
            {
                (void)Modem4G_HandleRetryableFailure();
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_REG_WAIT:
            if (g_status.registered != 0U)
            {
                Modem4G_SetState(MODEM4G_STATE_READY);
            }
            else if (Modem4G_IsElapsed(g_state_start_ms, MODEM4G_REG_POLL_MS) != 0U)
            {
                Modem4G_SetState(MODEM4G_STATE_CEREG_CHECK);
            }
            else
            {
            }
            break;

        case MODEM4G_STATE_READY:
            if (g_status.registered == 0U)
            {
                Modem4G_SetState(MODEM4G_STATE_REG_WAIT);
            }
            break;

        case MODEM4G_STATE_RESET_ASSERT:
            if (Modem4G_IsElapsed(g_state_start_ms, MODEM4G_RESET_HOLD_MS) != 0U)
            {
                PlatformPort_ModemResetRelease();
                Modem4G_BeginBootSyncWindow();
            }
            break;

        case MODEM4G_STATE_ERROR:
        default:
            break;
    }

}

void Modem4G_Init(void)
{
    Modem4G_ClearBytes(&g_modem_stats, (uint16_t)sizeof(g_modem_stats));
    Modem4G_ClearBytes(&g_status, (uint16_t)sizeof(g_status));
    g_status.state = MODEM4G_STATE_OFF;
    g_status.csq = MODEM4G_CSQ_UNKNOWN;
    g_status.cereg = MODEM4G_REG_INVALID;
    g_status.cgreg = MODEM4G_REG_INVALID;
    g_status.creg = MODEM4G_REG_INVALID;
    g_last_urc[0] = '\0';
    g_state_start_ms = PlatformPort_GetMs();
    g_boot_sync_start_ms = g_state_start_ms;
    g_retry_start_ms = g_state_start_ms;
    Modem4G_ResetCommandTracking();

    PlatformPort_ModemResetRelease();
    PlatformPort_ModemPowerKeyRelease();

    AT_Core_Init(&g_at_core,
                 Modem4G_Tx,
                 Modem4G_NowMs,
                 Modem4G_IsKnownUrc,
                 Modem4G_OnResponse,
                 Modem4G_OnUrc,
                 0);
}

void Modem4G_Start(void)
{
    if ((g_status.state != MODEM4G_STATE_OFF) && (g_status.state != MODEM4G_STATE_ERROR))
    {
        return;
    }

    AT_Core_Reset(&g_at_core);
    PlatformPort_ModemResetRelease();
    PlatformPort_ModemPowerKeyRelease();
    g_status.alive = 0U;
    g_status.sim_ready = 0U;
    g_status.registered = 0U;
    g_status.csq_valid = 0U;
    g_status.csq = MODEM4G_CSQ_UNKNOWN;
    g_status.cereg = MODEM4G_REG_INVALID;
    g_status.cgreg = MODEM4G_REG_INVALID;
    g_status.creg = MODEM4G_REG_INVALID;
    g_status.reset_attempts = 0U;
    g_status.last_ok_ms = 0U;
    Modem4G_ResetCommandTracking();
    Modem4G_SetState(MODEM4G_STATE_VBAT_SETTLE);
}

void Modem4G_Process(void)
{
    uint8_t buffer[MODEM4G_READ_CHUNK_SIZE];
    uint16_t count;

    do
    {
        count = PlatformPort_ModemRead(buffer, sizeof(buffer));
        if (count > 0U)
        {
            AT_Core_Feed(&g_at_core, buffer, count);
        }
    } while (count == sizeof(buffer));

    AT_Core_Process(&g_at_core);
    Modem4G_ProcessStateMachine();
}

uint8_t Modem4G_IsReady(void)
{
    return ((g_status.state == MODEM4G_STATE_READY) && (g_status.registered != 0U)) ? 1U : 0U;
}

Modem4G_State_T Modem4G_GetState(void)
{
    return g_status.state;
}

void Modem4G_GetStatus(Modem4G_Status_T *status)
{
    if (status != 0)
    {
        *status = g_status;
    }
}

AT_CoreStartResult_T Modem4G_StartTransaction(const AT_CoreTransaction_T *transaction)
{
    if ((g_status.state != MODEM4G_STATE_OFF) &&
        (g_status.state != MODEM4G_STATE_READY) &&
        (g_status.state != MODEM4G_STATE_ERROR))
    {
        return AT_CORE_START_BUSY;
    }

    return AT_Core_StartTransaction(&g_at_core, transaction);
}

AT_CoreStartResult_T Modem4G_StartCommand(const char *command,
                                           const char *response_prefix,
                                           const char *success_token,
                                           uint32_t timeout_ms)
{
    AT_CoreCommand_T request;

    if ((g_status.state != MODEM4G_STATE_OFF) &&
        (g_status.state != MODEM4G_STATE_READY) &&
        (g_status.state != MODEM4G_STATE_ERROR))
    {
        return AT_CORE_START_BUSY;
    }

    request.command = command;
    request.response_prefix = response_prefix;
    request.success_token = success_token;
    request.timeout_ms = timeout_ms;
    return AT_Core_StartCommand(&g_at_core, &request);
}

uint8_t Modem4G_IsBusy(void)
{
    return AT_Core_IsBusy(&g_at_core);
}

AT_CoreResult_T Modem4G_PeekResult(void)
{
    return AT_Core_PeekResult(&g_at_core);
}

AT_CoreResult_T Modem4G_TakeResult(void)
{
    return AT_Core_TakeResult(&g_at_core);
}

const AT_CoreStats_T *Modem4G_GetAtStats(void)
{
    return AT_Core_GetStats(&g_at_core);
}

void Modem4G_GetStats(Modem4G_Stats_T *stats)
{
    if (stats == 0)
    {
        return;
    }
    *stats = g_modem_stats;
}

uint16_t Modem4G_GetLastUrc(char *dst, uint16_t capacity)
{
    uint16_t length;

    if ((dst == 0) || (capacity == 0U))
    {
        return 0U;
    }

    length = Modem4G_StringLength(g_last_urc);
    if (length >= capacity)
    {
        length = capacity - 1U;
    }
    if (length > 0U)
    {
        Modem4G_CopyBytes(dst, g_last_urc, length);
    }
    dst[length] = '\0';
    return length;
}
