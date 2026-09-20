#include "gnss.h"

#include "bsp_tick.h"
#include "bsp_uart.h"
#include "nmea_parser.h"

#define GNSS_LINE_MAX_LENGTH  (128U)
#define GNSS_READ_CHUNK_SIZE   (64U)

static GNSS_Info_T g_info;
static GNSS_Stats_T g_stats;
static char g_line[GNSS_LINE_MAX_LENGTH];
static uint16_t g_line_length;
static uint8_t g_collecting;

static void Gnss_ClearInfo(void)
{
    g_info.valid = 0U;
    g_info.hour = 0U;
    g_info.minute = 0U;
    g_info.second = 0U;
    g_info.year = 0U;
    g_info.month = 0U;
    g_info.day = 0U;
    g_info.latitude_e7 = 0;
    g_info.longitude_e7 = 0;
    g_info.fix_quality = 0U;
    g_info.satellites = 0U;
    g_info.speed_cm_s = 0U;
    g_info.last_update_ms = 0U;
}

static void Gnss_ClearStats(void)
{
    g_stats.bytes = 0U;
    g_stats.sentences = 0U;
    g_stats.rmc_ok = 0U;
    g_stats.gga_ok = 0U;
    g_stats.checksum_error = 0U;
    g_stats.format_error = 0U;
    g_stats.overflow_error = 0U;
    g_stats.unsupported = 0U;
}

static void Gnss_ApplyDecoded(const NMEA_Decoded_T *decoded)
{
    if (decoded->has_time != 0U)
    {
        g_info.hour = decoded->hour;
        g_info.minute = decoded->minute;
        g_info.second = decoded->second;
    }

    if (decoded->has_date != 0U)
    {
        g_info.year = decoded->year;
        g_info.month = decoded->month;
        g_info.day = decoded->day;
    }

    if (decoded->has_fix_quality != 0U)
    {
        g_info.fix_quality = decoded->fix_quality;
    }

    if (decoded->has_satellites != 0U)
    {
        g_info.satellites = decoded->satellites;
    }

    if (decoded->has_speed != 0U)
    {
        g_info.speed_cm_s = decoded->speed_cm_s;
    }

    if (decoded->has_position != 0U)
    {
        g_info.latitude_e7 = decoded->latitude_e7;
        g_info.longitude_e7 = decoded->longitude_e7;
        g_info.last_update_ms = BSP_Tick_GetMs();
    }

    if (decoded->has_validity != 0U)
    {
        g_info.valid = decoded->valid;
    }
}

static void Gnss_ParseCurrentLine(void)
{
    NMEA_Decoded_T decoded;
    NMEA_ParseResult_T result;
    uint16_t length;

    length = g_line_length;
    if ((length > 0U) && (g_line[length - 1U] == '\r'))
    {
        length--;
    }

    if (length == 0U)
    {
        return;
    }

    g_stats.sentences++;
    result = NMEA_ParseSentence(g_line, length, &decoded);

    switch (result)
    {
        case NMEA_PARSE_OK_RMC:
            g_stats.rmc_ok++;
            Gnss_ApplyDecoded(&decoded);
            break;

        case NMEA_PARSE_OK_GGA:
            g_stats.gga_ok++;
            Gnss_ApplyDecoded(&decoded);
            break;

        case NMEA_PARSE_UNSUPPORTED:
            g_stats.unsupported++;
            break;

        case NMEA_PARSE_CHECKSUM_ERROR:
            g_stats.checksum_error++;
            break;

        case NMEA_PARSE_FORMAT_ERROR:
        default:
            g_stats.format_error++;
            break;
    }
}

void GNSS_Init(void)
{
    Gnss_ClearInfo();
    Gnss_ClearStats();
    g_line_length = 0U;
    g_collecting = 0U;
}

void GNSS_Process(void)
{
    uint8_t buffer[GNSS_READ_CHUNK_SIZE];
    uint16_t count;

    do
    {
        count = BSP_Uart_Read(BSP_UART_GNSS, buffer, sizeof(buffer));
        if (count > 0U)
        {
            GNSS_Feed(buffer, count);
        }
    } while (count == sizeof(buffer));
}

void GNSS_Feed(const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if ((data == 0) || (length == 0U))
    {
        return;
    }

    for (index = 0U; index < length; index++)
    {
        uint8_t byte;

        byte = data[index];
        g_stats.bytes++;

        if (byte == (uint8_t)'$')
        {
            if ((g_collecting != 0U) && (g_line_length > 0U))
            {
                g_stats.format_error++;
            }
            g_collecting = 1U;
            g_line_length = 0U;
            g_line[g_line_length++] = '$';
            continue;
        }

        if (g_collecting == 0U)
        {
            continue;
        }

        if (byte == (uint8_t)'\n')
        {
            Gnss_ParseCurrentLine();
            g_line_length = 0U;
            g_collecting = 0U;
            continue;
        }

        if (g_line_length >= (GNSS_LINE_MAX_LENGTH - 1U))
        {
            g_stats.overflow_error++;
            g_line_length = 0U;
            g_collecting = 0U;
            continue;
        }

        g_line[g_line_length++] = (char)byte;
    }
}

const GNSS_Info_T *GNSS_GetInfo(void)
{
    return &g_info;
}

void GNSS_GetStats(GNSS_Stats_T *stats)
{
    if (stats == 0)
    {
        return;
    }

    *stats = g_stats;
}

uint8_t GNSS_IsFresh(uint32_t timeout_ms)
{
    if (g_info.valid == 0U)
    {
        return 0U;
    }

    return ((uint32_t)(BSP_Tick_GetMs() - g_info.last_update_ms) <= timeout_ms) ? 1U : 0U;
}
