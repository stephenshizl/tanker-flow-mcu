#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform_port.h"
#include "modem_4g.h"

#define TEST_TX_MAX  (2048U)
#define TEST_RX_MAX  (2048U)

typedef struct
{
    uint32_t now_ms;
    char tx[TEST_TX_MAX];
    uint16_t tx_length;
    uint8_t rx[TEST_RX_MAX];
    uint16_t rx_length;
    uint16_t rx_offset;
    uint8_t reset_asserted;
    uint8_t powerkey_asserted;
    uint32_t reset_assert_count;
    uint32_t reset_release_count;
    uint32_t powerkey_assert_count;
    uint32_t powerkey_release_count;
} TestIo_T;

static TestIo_T g_io;
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

static void Test_ResetIo(void)
{
    (void)memset(&g_io, 0, sizeof(g_io));
}

static void Test_Advance(uint32_t delta_ms)
{
    g_io.now_ms += delta_ms;
    Modem4G_Process();
}

static void Test_Feed(const char *text)
{
    uint16_t length;

    length = (uint16_t)strlen(text);
    CHECK_TRUE((uint32_t)g_io.rx_length + length <= TEST_RX_MAX);
    if ((uint32_t)g_io.rx_length + length <= TEST_RX_MAX)
    {
        (void)memcpy(&g_io.rx[g_io.rx_length], text, length);
        g_io.rx_length = (uint16_t)(g_io.rx_length + length);
    }
    Modem4G_Process();
}

static void Test_ClearTx(void)
{
    g_io.tx_length = 0U;
    g_io.tx[0] = '\0';
}

static void Test_ExpectLastTx(const char *command)
{
    size_t length;

    length = strlen(command);
    CHECK_TRUE(g_io.tx_length >= length);
    if (g_io.tx_length >= length)
    {
        CHECK_TRUE(memcmp(&g_io.tx[g_io.tx_length - length], command, length) == 0);
    }
}

static uint8_t Test_MatchMipcall(const char *line, uint16_t length, void *user)
{
    static const char prefix[] = "+MIPCALL:";

    (void)user;
    if (length < (uint16_t)(sizeof(prefix) - 1U))
    {
        return 0U;
    }
    return (memcmp(line, prefix, sizeof(prefix) - 1U) == 0) ? 1U : 0U;
}

uint32_t PlatformPort_GetMs(void)
{
    return g_io.now_ms;
}


uint16_t PlatformPort_ModemRead(uint8_t *data, uint16_t max_length)
{
    uint16_t available;
    uint16_t copy_length;
    available = (uint16_t)(g_io.rx_length - g_io.rx_offset);
    copy_length = (available < max_length) ? available : max_length;
    if (copy_length > 0U)
    {
        (void)memcpy(data, &g_io.rx[g_io.rx_offset], copy_length);
        g_io.rx_offset = (uint16_t)(g_io.rx_offset + copy_length);
        if (g_io.rx_offset == g_io.rx_length)
        {
            g_io.rx_offset = 0U;
            g_io.rx_length = 0U;
        }
    }
    return copy_length;
}

uint16_t PlatformPort_ModemWrite(const uint8_t *data, uint16_t length)
{
    uint16_t room;
    uint16_t copy_length;
    room = (uint16_t)(TEST_TX_MAX - 1U - g_io.tx_length);
    copy_length = (length < room) ? length : room;
    if (copy_length > 0U)
    {
        (void)memcpy(&g_io.tx[g_io.tx_length], data, copy_length);
        g_io.tx_length = (uint16_t)(g_io.tx_length + copy_length);
        g_io.tx[g_io.tx_length] = '\0';
    }
    return copy_length;
}

void PlatformPort_ModemResetAssert(void)
{
    g_io.reset_asserted = 1U;
    g_io.reset_assert_count++;
}

void PlatformPort_ModemResetRelease(void)
{
    g_io.reset_asserted = 0U;
    g_io.reset_release_count++;
}

void PlatformPort_ModemPowerKeyAssert(void)
{
    g_io.powerkey_asserted = 1U;
    g_io.powerkey_assert_count++;
}

void PlatformPort_ModemPowerKeyRelease(void)
{
    g_io.powerkey_asserted = 0U;
    g_io.powerkey_release_count++;
}

static void Test_RunToAtSync(void)
{
    Modem4G_Init();
    Modem4G_Start();
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_VBAT_SETTLE);
    CHECK_TRUE(g_io.reset_asserted == 0U);
    CHECK_TRUE(g_io.powerkey_asserted == 0U);

    Test_Advance(29U);
    CHECK_TRUE(g_io.powerkey_asserted == 0U);
    Test_Advance(1U);
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_PWRKEY_ASSERT);
    CHECK_TRUE(g_io.powerkey_asserted != 0U);

    Test_Advance(1999U);
    CHECK_TRUE(g_io.powerkey_asserted != 0U);
    Test_Advance(1U);
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_AT_SYNC);
    CHECK_TRUE(g_io.powerkey_asserted == 0U);

    Modem4G_Process();
    Test_ExpectLastTx("AT\r\n");
}

static void Test_CompleteAtConfiguration(void)
{
    Test_Feed("AT\r\nOK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("ATQ0\r\n");

    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("ATV1\r\n");

    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("ATE0\r\n");

    Test_Feed("ATE0\r\nOK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CEREG=1\r\n");

    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CGREG=1\r\n");

    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CREG=1\r\n");

    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CPIN?\r\n");
}

static void Test_PowerSequence(void)
{
    Test_ResetIo();
    Test_RunToAtSync();
    CHECK_TRUE(g_io.powerkey_assert_count == 1U);
    CHECK_TRUE(g_io.powerkey_release_count >= 2U);
}

static void Test_HappyPathToReady(void)
{
    Modem4G_Status_T status;

    Test_ResetIo();
    Test_RunToAtSync();

    Test_CompleteAtConfiguration();

    Test_Feed("+CPIN: READY\r\nOK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CSQ\r\n");

    Test_Feed("+CSQ: 18,99\r\nOK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CEREG?\r\n");

    Test_Feed("+CEREG: 0,1\r\nOK\r\n");
    CHECK_TRUE(Modem4G_IsReady() != 0U);
    Modem4G_GetStatus(&status);
    CHECK_TRUE(status.alive != 0U);
    CHECK_TRUE(status.sim_ready != 0U);
    CHECK_TRUE(status.csq_valid != 0U);
    CHECK_TRUE(status.csq == 18U);
    CHECK_TRUE(status.cereg == 1U);
    CHECK_TRUE(status.registered != 0U);
}

static void Test_SimWaitAndRecovery(void)
{
    Test_ResetIo();
    Test_RunToAtSync();

    Test_CompleteAtConfiguration();
    Test_Feed("+CPIN: SIM PIN\r\nOK\r\n");
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_SIM_WAIT);

    Test_ClearTx();
    Test_Advance(2999U);
    CHECK_TRUE(g_io.tx_length == 0U);
    Test_Advance(1U);
    Modem4G_Process();
    Test_ExpectLastTx("AT+CPIN?\r\n");

    Test_Feed("+CPIN: READY\r\nOK\r\n");
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_SIGNAL_CHECK);
}

static void Test_RegistrationFallbackAndUrc(void)
{
    Modem4G_Status_T status;

    Test_ResetIo();
    Test_RunToAtSync();
    Test_CompleteAtConfiguration();
    Test_Feed("+CPIN: READY\r\nOK\r\n");
    Modem4G_Process();
    Test_Feed("+CSQ: 99,99\r\nOK\r\n");
    Modem4G_Process();

    Test_Feed("+CEREG: 0,2\r\nOK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CGREG?\r\n");
    Test_Feed("ERROR\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CREG?\r\n");
    Test_Feed("+CREG: 0,0\r\nOK\r\n");
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_REG_WAIT);

    Test_Feed("+CEREG: 1\r\n");
    CHECK_TRUE(Modem4G_IsReady() != 0U);
    Modem4G_GetStatus(&status);
    CHECK_TRUE(status.cereg == 1U);
    CHECK_TRUE(status.registered != 0U);
    CHECK_TRUE(status.csq_valid == 0U);
    CHECK_TRUE(status.csq == 99U);
}


static void Test_CgregFallbackRegistration(void)
{
    Modem4G_Status_T status;

    Test_ResetIo();
    Test_RunToAtSync();
    Test_CompleteAtConfiguration();
    Test_Feed("+CPIN: READY\r\nOK\r\n");
    Modem4G_Process();
    Test_Feed("+CSQ: 12,99\r\nOK\r\n");
    Modem4G_Process();
    Test_Feed("+CEREG: 0,2\r\nOK\r\n");
    Modem4G_Process();
    Test_Feed("+CGREG: 0,1\r\nOK\r\n");

    CHECK_TRUE(Modem4G_IsReady() != 0U);
    Modem4G_GetStatus(&status);
    CHECK_TRUE(status.cereg == 2U);
    CHECK_TRUE(status.cgreg == 1U);
    CHECK_TRUE(status.registered != 0U);
}


static void Test_ResponseLineCanArriveInFragments(void)
{
    Modem4G_Status_T status;

    Test_ResetIo();
    Test_RunToAtSync();
    Test_CompleteAtConfiguration();

    Test_Feed("+CP");
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_SIM_CHECK);
    Test_Feed("IN: RE");
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_SIM_CHECK);
    Test_Feed("ADY\r\n");
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_SIM_CHECK);
    Test_Feed("OK\r\n");

    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_SIGNAL_CHECK);
    Modem4G_GetStatus(&status);
    CHECK_TRUE(status.sim_ready != 0U);
}

static void Test_UrcEnableFailureIsNonFatal(void)
{
    Test_ResetIo();
    Test_RunToAtSync();

    Test_Feed("AT\r\nOK\r\n");
    Modem4G_Process();
    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CEREG=1\r\n");

    Test_Feed("ERROR\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CGREG=1\r\n");
    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CREG=1\r\n");
    Test_Feed("OK\r\n");
    Modem4G_Process();
    Test_ExpectLastTx("AT+CPIN?\r\n");
}

static void Test_UrcDispatcherWhileIdle(void)
{
    Modem4G_Status_T status;
    Modem4G_Stats_T stats;

    Test_ResetIo();
    Modem4G_Init();

    Test_Feed("AT READY\r\n+SIM READY\r\n");
    Modem4G_GetStatus(&status);
    Modem4G_GetStats(&stats);
    CHECK_TRUE(status.alive != 0U);
    CHECK_TRUE(status.sim_ready != 0U);
    CHECK_TRUE(stats.startup_urc_count == 2U);

    Test_Feed("+MIPRTCP: 1,0,3132\r\n");
    Test_Feed("+MIPRUDP: 1.2.3.4,1234,1,0,3132\r\n");
    Test_Feed("+MIPSTAT: 1,2\r\n");
    Test_Feed("+UNKNOWN: 1\r\n");
    Modem4G_GetStats(&stats);
    CHECK_TRUE(stats.miprtcp_count == 1U);
    CHECK_TRUE(stats.miprudp_count == 1U);
    CHECK_TRUE(stats.mipstat_count == 1U);
    CHECK_TRUE(stats.tcp_event_urc_count == 1U);
    CHECK_TRUE(stats.unknown_urc_count == 1U);
}

static void Test_AsyncTransactionKeepsUnrelatedUrcSeparate(void)
{
    AT_CoreTransaction_T transaction;
    Modem4G_Status_T status;

    Test_ResetIo();
    Test_RunToAtSync();
    Test_CompleteAtConfiguration();
    Test_Feed("+CPIN: READY\r\nOK\r\n");
    Modem4G_Process();
    Test_Feed("+CSQ: 18,99\r\nOK\r\n");
    Modem4G_Process();
    Test_Feed("+CEREG: 0,1\r\nOK\r\n");
    CHECK_TRUE(Modem4G_IsReady() != 0U);

    transaction.command = "AT+MIPCALL=1";
    transaction.response_prefix = 0;
    transaction.type = AT_CORE_TRANSACTION_ASYNC_OK;
    transaction.response_timeout_ms = 1000U;
    transaction.operation_timeout_ms = 30000U;
    transaction.async_match = Test_MatchMipcall;

    CHECK_TRUE(Modem4G_StartTransaction(&transaction) == AT_CORE_START_OK);
    Test_ExpectLastTx("AT+MIPCALL=1\r\n");
    Test_Feed("OK\r\n");
    CHECK_TRUE(Modem4G_IsBusy() != 0U);

    Test_Feed("+CEREG: 5\r\n");
    CHECK_TRUE(Modem4G_IsBusy() != 0U);
    Modem4G_GetStatus(&status);
    CHECK_TRUE(status.cereg == 5U);
    CHECK_TRUE(status.registered != 0U);

    Test_Feed("+MIPCALL:10.1.2.3\r\n");
    CHECK_TRUE(Modem4G_IsBusy() == 0U);
    CHECK_TRUE(Modem4G_TakeResult() == AT_CORE_RESULT_ASYNC_EVENT);
}

static void Test_BootTimeoutUsesHardwareReset(void)
{
    Test_ResetIo();
    Test_RunToAtSync();

    Test_Advance(1000U);
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_AT_SYNC);

    while ((Modem4G_GetState() == MODEM4G_STATE_AT_SYNC) && (g_io.now_ms < 40000U))
    {
        Test_Advance(500U);
    }

    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_RESET_ASSERT);
    CHECK_TRUE(g_io.reset_asserted != 0U);
    CHECK_TRUE(g_io.reset_assert_count == 1U);

    Test_Advance(99U);
    CHECK_TRUE(g_io.reset_asserted != 0U);
    Test_Advance(1U);
    CHECK_TRUE(g_io.reset_asserted == 0U);
    CHECK_TRUE(Modem4G_GetState() == MODEM4G_STATE_AT_SYNC);
}

int main(void)
{
    Test_PowerSequence();
    Test_HappyPathToReady();
    Test_SimWaitAndRecovery();
    Test_RegistrationFallbackAndUrc();
    Test_CgregFallbackRegistration();
    Test_ResponseLineCanArriveInFragments();
    Test_UrcEnableFailureIsNonFatal();
    Test_UrcDispatcherWhileIdle();
    Test_AsyncTransactionKeepsUnrelatedUrcSeparate();
    Test_BootTimeoutUsesHardwareReset();

    if (g_failures != 0)
    {
        printf("MC610 host tests: FAIL (%d)\n", g_failures);
        return 1;
    }

    printf("MC610 host tests: PASS\n");
    return 0;
}
