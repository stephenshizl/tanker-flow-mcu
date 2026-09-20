#include "nmea_parser.h"

#define NMEA_MAX_FIELDS  (20U)
#define NMEA_E7_SCALE    (10000000UL)

typedef struct
{
    const char *ptr;
    uint8_t length;
} NMEA_Field_T;

static uint8_t Nmea_HexValue(char ch, uint8_t *value)
{
    if ((ch >= '0') && (ch <= '9'))
    {
        *value = (uint8_t)(ch - '0');
        return 1U;
    }
    if ((ch >= 'A') && (ch <= 'F'))
    {
        *value = (uint8_t)(ch - 'A' + 10);
        return 1U;
    }
    if ((ch >= 'a') && (ch <= 'f'))
    {
        *value = (uint8_t)(ch - 'a' + 10);
        return 1U;
    }
    return 0U;
}

static uint8_t Nmea_ParseUnsigned(const NMEA_Field_T *field, uint32_t *value)
{
    uint8_t index;
    uint32_t result;

    if ((field == 0) || (value == 0) || (field->length == 0U))
    {
        return 0U;
    }

    result = 0U;
    for (index = 0U; index < field->length; index++)
    {
        char ch;

        ch = field->ptr[index];
        if ((ch < '0') || (ch > '9'))
        {
            return 0U;
        }
        {
            uint32_t digit;

            digit = (uint32_t)(ch - '0');
            if ((result > 429496729U) ||
                ((result == 429496729U) && (digit > 5U)))
            {
                return 0U;
            }
            result = (result * 10U) + digit;
        }
    }

    *value = result;
    return 1U;
}

static uint8_t Nmea_ParseTime(const NMEA_Field_T *field,
                              uint8_t *hour,
                              uint8_t *minute,
                              uint8_t *second)
{
    uint8_t hh;
    uint8_t mm;
    uint8_t ss;
    uint8_t index;

    if ((field == 0) || (hour == 0) || (minute == 0) || (second == 0) ||
        (field->length < 6U))
    {
        return 0U;
    }

    for (index = 0U; index < 6U; index++)
    {
        if ((field->ptr[index] < '0') || (field->ptr[index] > '9'))
        {
            return 0U;
        }
    }
    if ((field->length > 6U) && (field->ptr[6] != '.'))
    {
        return 0U;
    }

    hh = (uint8_t)(((field->ptr[0] - '0') * 10) + (field->ptr[1] - '0'));
    mm = (uint8_t)(((field->ptr[2] - '0') * 10) + (field->ptr[3] - '0'));
    ss = (uint8_t)(((field->ptr[4] - '0') * 10) + (field->ptr[5] - '0'));

    if ((hh > 23U) || (mm > 59U) || (ss > 60U))
    {
        return 0U;
    }

    if (field->length > 7U)
    {
        for (index = 7U; index < field->length; index++)
        {
            if ((field->ptr[index] < '0') || (field->ptr[index] > '9'))
            {
                return 0U;
            }
        }
    }

    *hour = hh;
    *minute = mm;
    *second = ss;
    return 1U;
}

static uint8_t Nmea_ParseDate(const NMEA_Field_T *field,
                              uint16_t *year,
                              uint8_t *month,
                              uint8_t *day)
{
    uint8_t dd;
    uint8_t mm;
    uint8_t yy;
    uint8_t index;

    if ((field == 0) || (year == 0) || (month == 0) || (day == 0) ||
        (field->length != 6U))
    {
        return 0U;
    }

    for (index = 0U; index < 6U; index++)
    {
        if ((field->ptr[index] < '0') || (field->ptr[index] > '9'))
        {
            return 0U;
        }
    }

    dd = (uint8_t)(((field->ptr[0] - '0') * 10) + (field->ptr[1] - '0'));
    mm = (uint8_t)(((field->ptr[2] - '0') * 10) + (field->ptr[3] - '0'));
    yy = (uint8_t)(((field->ptr[4] - '0') * 10) + (field->ptr[5] - '0'));

    if ((dd == 0U) || (dd > 31U) || (mm == 0U) || (mm > 12U))
    {
        return 0U;
    }

    *day = dd;
    *month = mm;
    *year = (yy >= 80U) ? (uint16_t)(1900U + yy) : (uint16_t)(2000U + yy);
    return 1U;
}

static uint8_t Nmea_ParseCoordinate(const NMEA_Field_T *field,
                                    char hemisphere,
                                    uint8_t degree_digits,
                                    int32_t *value_e7)
{
    uint8_t index;
    uint8_t dot_index;
    uint8_t fraction_digits;
    uint32_t degrees;
    uint32_t minutes;
    uint32_t minute_fraction_e7;
    uint32_t coordinate_e7;
    uint32_t scale;

    if ((field == 0) || (value_e7 == 0) ||
        (field->length < (uint8_t)(degree_digits + 2U)))
    {
        return 0U;
    }

    dot_index = field->length;
    for (index = 0U; index < field->length; index++)
    {
        char ch;

        ch = field->ptr[index];
        if (ch == '.')
        {
            if (dot_index != field->length)
            {
                return 0U;
            }
            dot_index = index;
        }
        else if ((ch < '0') || (ch > '9'))
        {
            return 0U;
        }
    }

    if (dot_index != (uint8_t)(degree_digits + 2U))
    {
        return 0U;
    }

    degrees = 0U;
    for (index = 0U; index < degree_digits; index++)
    {
        degrees = (degrees * 10U) + (uint32_t)(field->ptr[index] - '0');
    }

    minutes = (uint32_t)((field->ptr[degree_digits] - '0') * 10) +
              (uint32_t)(field->ptr[degree_digits + 1U] - '0');
    if (minutes >= 60U)
    {
        return 0U;
    }
    if (((degree_digits == 2U) && (degrees > 90U)) ||
        ((degree_digits == 3U) && (degrees > 180U)))
    {
        return 0U;
    }

    minute_fraction_e7 = 0U;
    fraction_digits = 0U;
    if (dot_index < field->length)
    {
        for (index = (uint8_t)(dot_index + 1U); index < field->length; index++)
        {
            if (fraction_digits < 7U)
            {
                minute_fraction_e7 = (minute_fraction_e7 * 10U) +
                                     (uint32_t)(field->ptr[index] - '0');
                fraction_digits++;
            }
        }
    }

    scale = 1U;
    while (fraction_digits < 7U)
    {
        scale *= 10U;
        fraction_digits++;
    }
    minute_fraction_e7 *= scale;

    if (((degree_digits == 2U) && (degrees == 90U) &&
         ((minutes != 0U) || (minute_fraction_e7 != 0U))) ||
        ((degree_digits == 3U) && (degrees == 180U) &&
         ((minutes != 0U) || (minute_fraction_e7 != 0U))))
    {
        return 0U;
    }

    coordinate_e7 = (degrees * NMEA_E7_SCALE) +
                    (((minutes * NMEA_E7_SCALE) + minute_fraction_e7) / 60U);

    if ((hemisphere == 'S') || (hemisphere == 'W'))
    {
        *value_e7 = -(int32_t)coordinate_e7;
    }
    else if ((hemisphere == 'N') || (hemisphere == 'E'))
    {
        *value_e7 = (int32_t)coordinate_e7;
    }
    else
    {
        return 0U;
    }

    return 1U;
}

static uint8_t Nmea_ParseKnotsToCmS(const NMEA_Field_T *field, uint32_t *speed_cm_s)
{
    uint8_t index;
    uint8_t seen_dot;
    uint8_t fraction_digits;
    uint32_t integer_part;
    uint32_t fraction_part;
    uint32_t knots_milli;

    if ((field == 0) || (speed_cm_s == 0) || (field->length == 0U))
    {
        return 0U;
    }

    seen_dot = 0U;
    fraction_digits = 0U;
    integer_part = 0U;
    fraction_part = 0U;

    for (index = 0U; index < field->length; index++)
    {
        char ch;

        ch = field->ptr[index];
        if (ch == '.')
        {
            if (seen_dot != 0U)
            {
                return 0U;
            }
            seen_dot = 1U;
            continue;
        }
        if ((ch < '0') || (ch > '9'))
        {
            return 0U;
        }

        if (seen_dot == 0U)
        {
            uint32_t digit;

            digit = (uint32_t)(ch - '0');
            if ((integer_part > 999U) ||
                ((integer_part == 999U) && (digit > 9U)))
            {
                return 0U;
            }
            integer_part = (integer_part * 10U) + digit;
        }
        else if (fraction_digits < 3U)
        {
            fraction_part = (fraction_part * 10U) + (uint32_t)(ch - '0');
            fraction_digits++;
        }
    }

    while (fraction_digits < 3U)
    {
        fraction_part *= 10U;
        fraction_digits++;
    }

    if (integer_part > 9999U)
    {
        return 0U;
    }

    knots_milli = (integer_part * 1000U) + fraction_part;
    *speed_cm_s = ((knots_milli / 9000U) * 463U) +
                  ((((knots_milli % 9000U) * 463U) + 4500U) / 9000U);
    return 1U;
}

static uint8_t Nmea_SentenceTypeEquals(const NMEA_Field_T *field,
                                       char a,
                                       char b,
                                       char c)
{
    if ((field == 0) || (field->length != 5U))
    {
        return 0U;
    }

    return ((field->ptr[2] == a) && (field->ptr[3] == b) && (field->ptr[4] == c)) ? 1U : 0U;
}

static uint8_t Nmea_SplitFields(const char *sentence,
                                uint16_t body_start,
                                uint16_t body_end,
                                NMEA_Field_T *fields,
                                uint8_t *field_count)
{
    uint16_t index;
    uint16_t start;
    uint8_t count;

    start = body_start;
    count = 0U;

    for (index = body_start; index <= body_end; index++)
    {
        if ((index == body_end) || (sentence[index] == ','))
        {
            if (count >= NMEA_MAX_FIELDS)
            {
                return 0U;
            }
            if ((uint16_t)(index - start) > 255U)
            {
                return 0U;
            }
            fields[count].ptr = &sentence[start];
            fields[count].length = (uint8_t)(index - start);
            count++;
            start = (uint16_t)(index + 1U);
        }
    }

    *field_count = count;
    return (count > 0U) ? 1U : 0U;
}

static NMEA_ParseResult_T Nmea_ParseRmc(const NMEA_Field_T *fields,
                                        uint8_t field_count,
                                        NMEA_Decoded_T *decoded)
{
    uint32_t speed;

    if (field_count < 10U)
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }

    if (Nmea_ParseTime(&fields[1], &decoded->hour, &decoded->minute, &decoded->second) == 0U)
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }
    decoded->has_time = 1U;

    if ((fields[2].length != 1U) ||
        ((fields[2].ptr[0] != 'A') && (fields[2].ptr[0] != 'V')))
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }
    decoded->has_validity = 1U;
    decoded->valid = (fields[2].ptr[0] == 'A') ? 1U : 0U;

    if (decoded->valid != 0U)
    {
        if ((fields[4].length != 1U) || (fields[6].length != 1U) ||
            (Nmea_ParseCoordinate(&fields[3], fields[4].ptr[0], 2U, &decoded->latitude_e7) == 0U) ||
            (Nmea_ParseCoordinate(&fields[5], fields[6].ptr[0], 3U, &decoded->longitude_e7) == 0U))
        {
            return NMEA_PARSE_FORMAT_ERROR;
        }
        decoded->has_position = 1U;
    }

    if (fields[7].length > 0U)
    {
        if (Nmea_ParseKnotsToCmS(&fields[7], &speed) == 0U)
        {
            return NMEA_PARSE_FORMAT_ERROR;
        }
        decoded->speed_cm_s = speed;
        decoded->has_speed = 1U;
    }

    if (Nmea_ParseDate(&fields[9], &decoded->year, &decoded->month, &decoded->day) == 0U)
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }
    decoded->has_date = 1U;

    return NMEA_PARSE_OK_RMC;
}

static NMEA_ParseResult_T Nmea_ParseGga(const NMEA_Field_T *fields,
                                        uint8_t field_count,
                                        NMEA_Decoded_T *decoded)
{
    uint32_t fix_quality;
    uint32_t satellites;

    if (field_count < 8U)
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }

    if (Nmea_ParseTime(&fields[1], &decoded->hour, &decoded->minute, &decoded->second) == 0U)
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }
    decoded->has_time = 1U;

    if ((Nmea_ParseUnsigned(&fields[6], &fix_quality) == 0U) || (fix_quality > 255U))
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }
    decoded->fix_quality = (uint8_t)fix_quality;
    decoded->has_fix_quality = 1U;
    decoded->has_validity = 1U;
    decoded->valid = (fix_quality > 0U) ? 1U : 0U;

    if ((Nmea_ParseUnsigned(&fields[7], &satellites) == 0U) || (satellites > 255U))
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }
    decoded->satellites = (uint8_t)satellites;
    decoded->has_satellites = 1U;

    if (decoded->valid != 0U)
    {
        if ((fields[3].length != 1U) || (fields[5].length != 1U) ||
            (Nmea_ParseCoordinate(&fields[2], fields[3].ptr[0], 2U, &decoded->latitude_e7) == 0U) ||
            (Nmea_ParseCoordinate(&fields[4], fields[5].ptr[0], 3U, &decoded->longitude_e7) == 0U))
        {
            return NMEA_PARSE_FORMAT_ERROR;
        }
        decoded->has_position = 1U;
    }

    return NMEA_PARSE_OK_GGA;
}

NMEA_ParseResult_T NMEA_ParseSentence(const char *sentence,
                                      uint16_t length,
                                      NMEA_Decoded_T *decoded)
{
    NMEA_Field_T fields[NMEA_MAX_FIELDS];
    uint16_t star_index;
    uint16_t index;
    uint8_t field_count;
    uint8_t checksum;
    uint8_t high;
    uint8_t low;

    if ((sentence == 0) || (decoded == 0) || (length < 7U) || (sentence[0] != '$'))
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }

    decoded->has_time = 0U;
    decoded->has_date = 0U;
    decoded->has_position = 0U;
    decoded->has_validity = 0U;
    decoded->has_fix_quality = 0U;
    decoded->has_satellites = 0U;
    decoded->has_speed = 0U;
    decoded->valid = 0U;

    star_index = length;
    for (index = 1U; index < length; index++)
    {
        if (sentence[index] == '*')
        {
            star_index = index;
            break;
        }
    }

    if ((star_index == length) || (star_index < 2U) || ((uint16_t)(star_index + 3U) != length))
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }

    checksum = 0U;
    for (index = 1U; index < star_index; index++)
    {
        checksum ^= (uint8_t)sentence[index];
    }

    if ((Nmea_HexValue(sentence[star_index + 1U], &high) == 0U) ||
        (Nmea_HexValue(sentence[star_index + 2U], &low) == 0U))
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }
    if (checksum != (uint8_t)((high << 4) | low))
    {
        return NMEA_PARSE_CHECKSUM_ERROR;
    }

    if (Nmea_SplitFields(sentence, 1U, star_index, fields, &field_count) == 0U)
    {
        return NMEA_PARSE_FORMAT_ERROR;
    }

    if (Nmea_SentenceTypeEquals(&fields[0], 'R', 'M', 'C') != 0U)
    {
        return Nmea_ParseRmc(fields, field_count, decoded);
    }
    if (Nmea_SentenceTypeEquals(&fields[0], 'G', 'G', 'A') != 0U)
    {
        return Nmea_ParseGga(fields, field_count, decoded);
    }

    return NMEA_PARSE_UNSUPPORTED;
}
