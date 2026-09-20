#ifndef NMEA_PARSER_H
#define NMEA_PARSER_H

#include <stdint.h>

typedef enum
{
    NMEA_PARSE_OK_RMC = 0,
    NMEA_PARSE_OK_GGA,
    NMEA_PARSE_UNSUPPORTED,
    NMEA_PARSE_CHECKSUM_ERROR,
    NMEA_PARSE_FORMAT_ERROR
} NMEA_ParseResult_T;

typedef struct
{
    uint8_t has_time;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    uint8_t has_date;
    uint16_t year;
    uint8_t month;
    uint8_t day;

    uint8_t has_position;
    int32_t latitude_e7;
    int32_t longitude_e7;

    uint8_t has_validity;
    uint8_t valid;

    uint8_t has_fix_quality;
    uint8_t fix_quality;

    uint8_t has_satellites;
    uint8_t satellites;

    uint8_t has_speed;
    uint32_t speed_cm_s;
} NMEA_Decoded_T;

NMEA_ParseResult_T NMEA_ParseSentence(const char *sentence,
                                      uint16_t length,
                                      NMEA_Decoded_T *decoded);

#endif /* NMEA_PARSER_H */
