#ifndef GNSS_H
#define GNSS_H

#include <stdint.h>

typedef struct
{
    uint8_t valid;

    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    uint16_t year;
    uint8_t month;
    uint8_t day;

    int32_t latitude_e7;
    int32_t longitude_e7;

    uint8_t fix_quality;
    uint8_t satellites;
    uint32_t speed_cm_s;

    uint32_t last_update_ms;
} GNSS_Info_T;

typedef struct
{
    uint32_t bytes;
    uint32_t sentences;
    uint32_t rmc_ok;
    uint32_t gga_ok;
    uint32_t checksum_error;
    uint32_t format_error;
    uint32_t overflow_error;
    uint32_t unsupported;
} GNSS_Stats_T;

void GNSS_Init(void);
void GNSS_Process(void);
void GNSS_Feed(const uint8_t *data, uint16_t length);
const GNSS_Info_T *GNSS_GetInfo(void);
void GNSS_GetStats(GNSS_Stats_T *stats);
uint8_t GNSS_IsFresh(uint32_t timeout_ms);

#endif /* GNSS_H */
