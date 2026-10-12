#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "at_core.h"
#include "mc610_tcp.h"
#include "modem_4g.h"
#include "platform_port.h"

typedef struct
{
    uint32_t now_ms;
    uint8_t modem_ready;
    uint8_t modem_busy;
    AT_CoreResult_T completed_result;
    AT_CoreTransaction_T last_transaction;
    char last_command[96];
    Modem4G_NetworkEvent_T events[8];
    uint8_t event_head;
    uint8_t event_tail;
    uint8_t event_count;
} TestCtx_T;

static TestCtx_T g_test;
static int g_failures;

#define CHECK_TRUE(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            g_failures++; \
        } \
    } while (0)

static void Test_Reset(void)
{
    (void)memset(&g_test, 0, sizeof(g_test));
    g_test.completed_result = AT_CORE_RESULT_NONE;
}

static void Test_PushEvent(Modem4G_NetworkEventType_T type,
                           uint8_t socket_id,
                           uint8_t result)
{
    Modem4G_NetworkEvent_T *event;

    CHECK_TRUE(g_test.event_count < 8U);
    if (g_test.event_count >= 8U)
    {
        return;
    }
    event = &g_test.events[g_test.event_head];
    (void)memset(event, 0, sizeof(*event));
    event->type = type;
    event->socket_id = socket_id;
    event->result = result;
    g_test.event_head = (uint8_t)((g_test.event_head + 1U) % 8U);
    g_test.event_count++;
}

static void Test_CompleteAsync(Modem4G_NetworkEventType_T type,
                               uint8_t socket_id,
                               uint8_t result)
{
    Test_PushEvent(type, socket_id, result);
    g_test.modem_busy = 0U;
    g_test.completed_result = AT_CORE_RESULT_ASYNC_EVENT;
    MC610Tcp_Process();
}

uint32_t PlatformPort_GetMs(void)
{
    return g_test.now_ms;
}

uint8_t Modem4G_IsReady(void)
{
    return g_test.modem_ready;
}

AT_CoreStartResult_T Modem4G_StartTransaction(const AT_CoreTransaction_T *transaction)
{
    size_t length;

    if (g_test.modem_busy != 0U)
    {
        return AT_CORE_START_BUSY;
    }
    g_test.last_transaction = *transaction;
    length = strlen(transaction->command);
    CHECK_TRUE(length < sizeof(g_test.last_command));
    if (length >= sizeof(g_test.last_command))
    {
        return AT_CORE_START_INVALID;
    }
    (void)memcpy(g_test.last_command, transaction->command, length + 1U);
    g_test.modem_busy = 1U;
    g_test.completed_result = AT_CORE_RESULT_NONE;
    return AT_CORE_START_OK;
}

uint8_t Modem4G_IsBusy(void)
{
    return g_test.modem_busy;
}

AT_CoreResult_T Modem4G_TakeResult(void)
{
    AT_CoreResult_T result;

    result = g_test.completed_result;
    g_test.completed_result = AT_CORE_RESULT_NONE;
    return result;
}

uint8_t Modem4G_PopNetworkEvent(Modem4G_NetworkEvent_T *event)
{
    if ((event == 0) || (g_test.event_count == 0U))
    {
        return 0U;
    }
    *event = g_test.events[g_test.event_tail];
    g_test.event_tail = (uint8_t)((g_test.event_tail + 1U) % 8U);
    g_test.event_count--;
    return 1U;
}

static MC610Tcp_Config_T Test_Config(void)
{
    MC610Tcp_Config_T config;

    (void)memset(&config, 0, sizeof(config));
    (void)strcpy(config.apn, "CMNET");
    (void)strcpy(config.remote_host, "server.example.com");
    config.remote_port = 3417U;
    config.socket_id = 1U;
    config.auto_reconnect = 1U;
    config.reconnect_delay_ms = 1000U;
    return config;
}

static void Test_ConnectSequence(void)
{
    MC610Tcp_Config_T config;
    MC610Tcp_Status_T status;

    Test_Reset();
    config = Test_Config();
    CHECK_TRUE(MC610Tcp_Init(&config) == MC610_TCP_OK);
    CHECK_TRUE(MC610Tcp_Start() == MC610_TCP_OK);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_WAIT_MODEM);

    MC610Tcp_Process();
    CHECK_TRUE(g_test.last_command[0] == '\0');

    g_test.modem_ready = 1U;
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_PDP_ACTIVATING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPCALL=1,\"CMNET\"") == 0);
    CHECK_TRUE(g_test.last_transaction.type == AT_CORE_TRANSACTION_ASYNC_OK);
    CHECK_TRUE(g_test.last_transaction.operation_timeout_ms == 30000U);

    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCALL, 0U, 1U);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_SOCKET_OPENING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command,
                      "AT+MIPOPEN=1,\"server.example.com\",3417,0") == 0);
    CHECK_TRUE(g_test.last_transaction.operation_timeout_ms == 5000U);

    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPOPEN, 1U, 1U);
    CHECK_TRUE(MC610Tcp_IsConnected() != 0U);
    MC610Tcp_GetStatus(&status);
    CHECK_TRUE(status.pdp_active != 0U);
    CHECK_TRUE(status.connected != 0U);
}

static void Test_RemoteCloseSchedulesReconnect(void)
{
    MC610Tcp_Config_T config;

    Test_Reset();
    config = Test_Config();
    g_test.modem_ready = 1U;
    CHECK_TRUE(MC610Tcp_Init(&config) == MC610_TCP_OK);
    CHECK_TRUE(MC610Tcp_Start() == MC610_TCP_OK);
    MC610Tcp_Process();
    MC610Tcp_Process();
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCALL, 0U, 1U);
    MC610Tcp_Process();
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPOPEN, 1U, 1U);
    CHECK_TRUE(MC610Tcp_IsConnected() != 0U);

    Test_PushEvent(MODEM4G_NETWORK_EVENT_MIPSTAT, 1U, 2U);
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_RETRY_WAIT);
    CHECK_TRUE(MC610Tcp_IsConnected() == 0U);

    g_test.now_ms += 999U;
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_RETRY_WAIT);
    g_test.now_ms += 1U;
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_SOCKET_OPENING);
}

static void Test_StopClosesSocketAndPdp(void)
{
    MC610Tcp_Config_T config;

    Test_Reset();
    config = Test_Config();
    g_test.modem_ready = 1U;
    CHECK_TRUE(MC610Tcp_Init(&config) == MC610_TCP_OK);
    CHECK_TRUE(MC610Tcp_Start() == MC610_TCP_OK);
    MC610Tcp_Process();
    MC610Tcp_Process();
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCALL, 0U, 1U);
    MC610Tcp_Process();
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPOPEN, 1U, 1U);

    MC610Tcp_Stop();
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_SOCKET_CLOSING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPCLOSE=1") == 0);
    CHECK_TRUE(g_test.last_transaction.operation_timeout_ms == 3000U);

    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCLOSE, 1U, 0U);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_PDP_DEACTIVATING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPCALL=0") == 0);
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCALL, 0U, 0U);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_STOPPED);
}

static void Test_InvalidConfigRejected(void)
{
    MC610Tcp_Config_T config;

    Test_Reset();
    config = Test_Config();
    config.remote_port = 0U;
    CHECK_TRUE(MC610Tcp_Init(&config) == MC610_TCP_ERR_INVALID_ARG);
}

int main(void)
{
    Test_ConnectSequence();
    Test_RemoteCloseSchedulesReconnect();
    Test_StopClosesSocketAndPdp();
    Test_InvalidConfigRejected();

    if (g_failures != 0)
    {
        printf("MC610 TCP host tests: FAIL (%d)\n", g_failures);
        return 1;
    }

    printf("MC610 TCP host tests: PASS\n");
    return 0;
}
