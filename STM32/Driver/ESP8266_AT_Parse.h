#ifndef ESP8266_AT_PARSE_H
#define ESP8266_AT_PARSE_H

#include <stdint.h>

/*
 * Parses a line beginning with +CIPSTA:gateway: and copies the non-empty
 * quoted value to out.
 *
 * Returns 0 on success, -1 when line is not a gateway line, -2 for invalid
 * arguments or malformed quoting, and -3 when out is too small. On any
 * failure, out is left unchanged.
 */
int esp8266_at_parse_gateway(const char *line, char *out, uint16_t out_len);

#endif
