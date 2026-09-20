#include "modem_4g.h"

#include "bsp_tick.h"
#include "bsp_uart.h"

#define MODEM4G_READ_CHUNK_SIZE  (128U)
#define MODEM4G_LAST_URC_MAX     (128U)

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
static char g_last_urc[MODEM4G_LAST_URC_MAX];

static uint8_t Modem4G_StartsWith(const char *line, uint16_t length, const char *prefix)
{
    uint16_t prefix_length;

    prefix_length = Modem4G_StringLength(prefix);
    if (length < prefix_length)
    {
        return 0U;
    }
    {
        uint16_t index;
        for (index = 0U; index < prefix_length; index++)
        {
            if (line[index] != prefix[index])
            {
                return 0U;
            }
        }
    }
    return 1U;
}

static uint16_t Modem4G_Tx(const uint8_t *data, uint16_t length, void *user)
{
    (void)user;
    BSP_Uart_Write(BSP_UART_4G, data, length);
    return length;
}

static uint32_t Modem4G_NowMs(void *user)
{
    (void)user;
    return BSP_Tick_GetMs();
}

static uint8_t Modem4G_IsKnownUrc(const char *line, uint16_t length, void *user)
{
    (void)user;

    if ((Modem4G_StartsWith(line, length, "+MIPRTCP:") != 0U) ||
        (Modem4G_StartsWith(line, length, "+MIPSTAT") != 0U) ||
        (Modem4G_StartsWith(line, length, "+MIPCLOSE") != 0U) ||
        (Modem4G_StartsWith(line, length, "+CEREG:") != 0U) ||
        (Modem4G_StartsWith(line, length, "+CGREG:") != 0U) ||
        (Modem4G_StartsWith(line, length, "+CREG:") != 0U) ||
        (Modem4G_StartsWith(line, length, "RDY") != 0U) ||
        (Modem4G_StartsWith(line, length, "PB DONE") != 0U) ||
        (Modem4G_StartsWith(line, length, "SMS Ready") != 0U) ||
        (Modem4G_StartsWith(line, length, "Call Ready") != 0U))
    {
        return 1U;
    }
    return 0U;
}

static void Modem4G_OnResponse(const char *line, uint16_t length, void *user)
{
    (void)line;
    (void)length;
    (void)user;
}

static void Modem4G_OnUrc(const char *line, uint16_t length, void *user)
{
    uint16_t copy_length;

    (void)user;
    g_modem_stats.urc_count++;

    if (Modem4G_StartsWith(line, length, "+MIPRTCP:") != 0U)
    {
        g_modem_stats.miprtcp_count++;
    }
    else if (Modem4G_StartsWith(line, length, "+MIPSTAT") != 0U)
    {
        g_modem_stats.mipstat_count++;
    }
    else if ((Modem4G_StartsWith(line, length, "+CEREG:") != 0U) ||
             (Modem4G_StartsWith(line, length, "+CGREG:") != 0U) ||
             (Modem4G_StartsWith(line, length, "+CREG:") != 0U))
    {
        g_modem_stats.network_urc_count++;
    }
    else
    {
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

void Modem4G_Init(void)
{
    {
        uint8_t *bytes;
        uint16_t index;

        bytes = (uint8_t *)&g_modem_stats;
        for (index = 0U; index < (uint16_t)sizeof(g_modem_stats); index++)
        {
            bytes[index] = 0U;
        }
    }
    g_last_urc[0] = '\0';
    AT_Core_Init(&g_at_core,
                 Modem4G_Tx,
                 Modem4G_NowMs,
                 Modem4G_IsKnownUrc,
                 Modem4G_OnResponse,
                 Modem4G_OnUrc,
                 0);
}

void Modem4G_Process(void)
{
    uint8_t buffer[MODEM4G_READ_CHUNK_SIZE];
    uint16_t count;

    do
    {
        count = BSP_Uart_Read(BSP_UART_4G, buffer, sizeof(buffer));
        if (count > 0U)
        {
            AT_Core_Feed(&g_at_core, buffer, count);
        }
    } while (count == sizeof(buffer));

    AT_Core_Process(&g_at_core);
}

AT_CoreStartResult_T Modem4G_StartCommand(const char *command,
                                           const char *response_prefix,
                                           const char *success_token,
                                           uint32_t timeout_ms)
{
    AT_CoreCommand_T request;

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

AT_CoreResult_T Modem4G_TakeResult(char *response, uint16_t response_capacity)
{
    return AT_Core_TakeResult(&g_at_core, response, response_capacity);
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
