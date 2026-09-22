#include <stdio.h>
#include <string.h>

#include "platform_port.h"
#include "gnss.h"

static uint32_t g_mock_tick_ms;
static unsigned int g_failures;

uint32_t PlatformPort_GetMs(void)
{
    return g_mock_tick_ms;
}

uint16_t PlatformPort_GnssRead(uint8_t *data, uint16_t max_length)
{
    (void)data;
    (void)max_length;
    return 0U;
}

static void Test_Check(int condition, const char *name)
{
    if (condition != 0)
    {
        printf("[PASS] %s\n", name);
    }
    else
    {
        printf("[FAIL] %s\n", name);
        g_failures++;
    }
}

static uint16_t Test_BuildSentence(const char *body, char *output, uint16_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t checksum;
    uint16_t body_length;
    uint16_t index;

    body_length = (uint16_t)strlen(body);
    if (capacity < (uint16_t)(body_length + 7U))
    {
        return 0U;
    }

    checksum = 0U;
    output[0] = '$';
    for (index = 0U; index < body_length; index++)
    {
        output[index + 1U] = body[index];
        checksum ^= (uint8_t)body[index];
    }

    output[body_length + 1U] = '*';
    output[body_length + 2U] = hex[(checksum >> 4) & 0x0FU];
    output[body_length + 3U] = hex[checksum & 0x0FU];
    output[body_length + 4U] = '\r';
    output[body_length + 5U] = '\n';
    output[body_length + 6U] = '\0';
    return (uint16_t)(body_length + 6U);
}

static void Test_NormalRmc(void)
{
    char sentence[160];
    uint16_t length;
    const GNSS_Info_T *info;
    GNSS_Stats_T stats;

    GNSS_Init();
    g_mock_tick_ms = 1234U;
    length = Test_BuildSentence("GNRMC,092751.000,A,2232.12345,N,11403.45678,E,0.13,309.62,120926,,,A",
                                sentence,
                                sizeof(sentence));
    GNSS_Feed((const uint8_t *)sentence, length);
    info = GNSS_GetInfo();
    GNSS_GetStats(&stats);

    Test_Check(info->valid == 1U, "RMC normal valid");
    Test_Check((info->hour == 9U) && (info->minute == 27U) && (info->second == 51U), "RMC UTC");
    Test_Check((info->year == 2026U) && (info->month == 9U) && (info->day == 12U), "RMC date");
    Test_Check(info->latitude_e7 == 225353908, "RMC latitude e7");
    Test_Check(info->longitude_e7 == 1140576130, "RMC longitude e7");
    Test_Check(info->speed_cm_s == 7U, "RMC speed cm/s");
    Test_Check(info->last_update_ms == 1234U, "RMC last update tick");
    Test_Check((stats.rmc_ok == 1U) && (stats.checksum_error == 0U), "RMC statistics");
}

static void Test_InvalidRmc(void)
{
    char sentence[160];
    uint16_t length;
    const GNSS_Info_T *info;

    GNSS_Init();
    length = Test_BuildSentence("GPRMC,092751.000,V,,,,,0.00,0.00,120926,,,N",
                                sentence,
                                sizeof(sentence));
    GNSS_Feed((const uint8_t *)sentence, length);
    info = GNSS_GetInfo();

    Test_Check(info->valid == 0U, "RMC invalid status");
    Test_Check((info->year == 2026U) && (info->month == 9U) && (info->day == 12U), "RMC invalid still updates date");
}

static void Test_NormalGga(void)
{
    char sentence[160];
    uint16_t length;
    const GNSS_Info_T *info;
    GNSS_Stats_T stats;

    GNSS_Init();
    g_mock_tick_ms = 2500U;
    length = Test_BuildSentence("GNGGA,092752.000,2232.12345,N,11403.45678,E,1,10,0.9,12.3,M,-2.0,M,,",
                                sentence,
                                sizeof(sentence));
    GNSS_Feed((const uint8_t *)sentence, length);
    info = GNSS_GetInfo();
    GNSS_GetStats(&stats);

    Test_Check(info->valid == 1U, "GGA valid fix");
    Test_Check((info->fix_quality == 1U) && (info->satellites == 10U), "GGA fix/satellite fields");
    Test_Check((info->latitude_e7 == 225353908) && (info->longitude_e7 == 1140576130), "GGA position");
    Test_Check(stats.gga_ok == 1U, "GGA statistics");
}

static void Test_ChecksumReject(void)
{
    char sentence[160];
    uint16_t length;
    GNSS_Stats_T stats;

    GNSS_Init();
    length = Test_BuildSentence("GNRMC,092751.000,A,2232.12345,N,11403.45678,E,0.13,309.62,120926,,,A",
                                sentence,
                                sizeof(sentence));
    sentence[10] = (sentence[10] == '0') ? '1' : '0';
    GNSS_Feed((const uint8_t *)sentence, length);
    GNSS_GetStats(&stats);

    Test_Check((stats.checksum_error == 1U) && (stats.rmc_ok == 0U), "checksum reject");
}

static void Test_SplitAndConcatenated(void)
{
    char rmc[160];
    char gga[160];
    char combined[320];
    uint16_t rmc_length;
    uint16_t gga_length;
    GNSS_Stats_T stats;

    GNSS_Init();
    rmc_length = Test_BuildSentence("GNRMC,092751.000,A,2232.12345,N,11403.45678,E,0.13,309.62,120926,,,A",
                                    rmc,
                                    sizeof(rmc));
    GNSS_Feed((const uint8_t *)rmc, 17U);
    GNSS_Feed((const uint8_t *)&rmc[17], (uint16_t)(rmc_length - 17U));
    GNSS_GetStats(&stats);
    Test_Check(stats.rmc_ok == 1U, "split packet");

    GNSS_Init();
    gga_length = Test_BuildSentence("GPGGA,092752.000,2232.12345,N,11403.45678,E,1,10,0.9,12.3,M,-2.0,M,,",
                                    gga,
                                    sizeof(gga));
    memcpy(combined, rmc, rmc_length);
    memcpy(&combined[rmc_length], gga, gga_length);
    GNSS_Feed((const uint8_t *)combined, (uint16_t)(rmc_length + gga_length));
    GNSS_GetStats(&stats);
    Test_Check((stats.rmc_ok == 1U) && (stats.gga_ok == 1U) && (stats.sentences == 2U), "concatenated packets");
}

static void Test_GarbageOverflowAndRecovery(void)
{
    char sentence[160];
    uint8_t garbage[16] = {0xA5U, 0x00U, 0xFFU, 'x', 'x', 'x'};
    uint8_t overflow[150];
    uint16_t length;
    uint16_t index;
    GNSS_Stats_T stats;

    GNSS_Init();
    GNSS_Feed(garbage, sizeof(garbage));
    length = Test_BuildSentence("GNRMC,092751.000,A,2232.12345,N,11403.45678,E,0.13,309.62,120926,,,A",
                                sentence,
                                sizeof(sentence));
    GNSS_Feed((const uint8_t *)sentence, length);
    GNSS_GetStats(&stats);
    Test_Check(stats.rmc_ok == 1U, "garbage prefix resync");

    GNSS_Init();
    overflow[0] = '$';
    for (index = 1U; index < sizeof(overflow); index++)
    {
        overflow[index] = 'A';
    }
    GNSS_Feed(overflow, sizeof(overflow));
    GNSS_Feed((const uint8_t *)"\r\n", 2U);
    GNSS_Feed((const uint8_t *)sentence, length);
    GNSS_GetStats(&stats);
    Test_Check((stats.overflow_error == 1U) && (stats.rmc_ok == 1U), "overflow recovery");
}

static void Test_HemisphereAndFreshness(void)
{
    char sentence[160];
    uint16_t length;
    const GNSS_Info_T *info;

    GNSS_Init();
    g_mock_tick_ms = 1000U;
    length = Test_BuildSentence("GPRMC,092751.000,A,2232.12345,S,11403.45678,W,1.00,0.00,120926,,,A",
                                sentence,
                                sizeof(sentence));
    GNSS_Feed((const uint8_t *)sentence, length);
    info = GNSS_GetInfo();

    Test_Check((info->latitude_e7 == -225353908) && (info->longitude_e7 == -1140576130), "south/west conversion");
    g_mock_tick_ms = 5000U;
    Test_Check(GNSS_IsFresh(5000U) == 1U, "fresh fix");
    g_mock_tick_ms = 7001U;
    Test_Check(GNSS_IsFresh(5000U) == 0U, "stale fix");
}

static void Test_Unsupported(void)
{
    char sentence[80];
    uint16_t length;
    GNSS_Stats_T stats;

    GNSS_Init();
    length = Test_BuildSentence("GNTXT,01,01,02,TEST", sentence, sizeof(sentence));
    GNSS_Feed((const uint8_t *)sentence, length);
    GNSS_GetStats(&stats);
    Test_Check(stats.unsupported == 1U, "unsupported sentence");
}

int main(void)
{
    Test_NormalRmc();
    Test_InvalidRmc();
    Test_NormalGga();
    Test_ChecksumReject();
    Test_SplitAndConcatenated();
    Test_GarbageOverflowAndRecovery();
    Test_HemisphereAndFreshness();
    Test_Unsupported();

    if (g_failures == 0U)
    {
        printf("GNSS host tests: PASS\n");
        return 0;
    }

    printf("GNSS host tests: FAIL (%u)\n", g_failures);
    return 1;
}
