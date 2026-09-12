#include "ESP8266_AT_Parse.h"

#include <stddef.h>
#include <string.h>

int esp8266_at_parse_gateway(const char *line, char *out, uint16_t out_len)
{
    static const char marker[] = "+CIPSTA:gateway:";
    const char *opening_quote;
    const char *closing_quote;
    size_t value_length;

    if ((line == NULL) || (out == NULL) || (out_len == 0U)) {
        return -2;
    }

    if (strncmp(line, marker, sizeof(marker) - 1U) != 0) {
        return -1;
    }

    opening_quote = line + sizeof(marker) - 1U;
    if (*opening_quote != '"') {
        return -2;
    }

    closing_quote = strchr(opening_quote + 1, '"');
    if ((closing_quote == NULL) || (closing_quote == opening_quote + 1)) {
        return -2;
    }

    value_length = (size_t)(closing_quote - opening_quote - 1);
    if (value_length >= (size_t)out_len) {
        return -3;
    }

    memcpy(out, opening_quote + 1, value_length);
    out[value_length] = '\0';

    return 0;
}
