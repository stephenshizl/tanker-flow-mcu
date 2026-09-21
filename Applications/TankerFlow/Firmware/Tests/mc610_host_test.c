#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_gpio.h"
#include "bsp_tick.h"
#include "bsp_uart.h"
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

uint32_t BSP_Tick_GetMs(void)
{
    return g_io.now_ms;
}

uint8_t BSP_Tick_Elapsed(uint32_t start_ms, uint32_t period_ms)
{
    return ((uint32_t)(g_io.now_ms - start_ms) >= period_ms) ? 1U : 0U;
}

uint16_t BSP_Uart_Read(BSP_UartPort_T port, uint8_t *data, uint16_t max_length)
{
    uint16_t available;
    uint16_t copy_length;

    CHECK_TRUE(port == BSP_UART_4G);
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

void BSP_Uart_Write(BSP_UartPort_T port, const uint8_t *data, uint16_t length)
{
    uint16_t room;
    uint16_t copy_length;

    CHECK_TRUE(port == BSP_UART_4G);
    room = (uint16_t)(TEST_TX_MAX - 1U - g_io.tx_length);
    copy_length = (length < room) ? length : room;
    if (copy_length > 0U)
    {
        (void)memcpy(&g_io.tx[g_io.tx_length], data, copy_length);
        g_io.tx_length = (uint16_t)(g_io.tx_length + copy_length);
        g_io.tx[g_io.tx_length] = '\0';
    }
}

void BSP_4G_ResetAssert(void)
{
    g_io.reset_asserted = 1U;
    g_io.reset_assert_count++;
}

void BSP_4G_ResetRelease(void)
{
    g_io.reset_asserted = 0U;
    g_io.reset_release_count++;
}

void BSP_4G_PowerKeyAssert(void)
{
    g_io.powerkey_asserted = 1U;
    g_io.powerkey_assert_count++;
}

void BSP_4G_PowerKeyRelease(void)
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
    Test_BootTimeoutUsesHardwareReset();

    if (g_failures != 0)
    {
        printf("MC610 host tests: FAIL (%d)\n", g_failures);
        return 1;
    }

    printf("MC610 host tests: PASS\n");
    return 0;
}
