#include "ESP8266_AT_Parse.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,         \
                    #condition);                                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

int main(void)
{
    static const char unchanged[] = "unchanged";
    char output[32] = {0};
    char exact_output[8] = {0};
    char short_output[7] = "same";

    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:\"192.168.137.1\"\r\n",
              output,
              (uint16_t)sizeof(output)) == 0);
    CHECK(strcmp(output, "192.168.137.1") == 0);

    memset(output, 0, sizeof(output));
    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:\"192.168.173.1\"",
              output,
              (uint16_t)sizeof(output)) == 0);
    CHECK(strcmp(output, "192.168.173.1") == 0);

    memcpy(output, unchanged, sizeof(unchanged));
    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:ip:\"192.168.137.20\"",
              output,
              (uint16_t)sizeof(output)) == -1);
    CHECK(strcmp(output, unchanged) == 0);

    memcpy(output, unchanged, sizeof(unchanged));
    CHECK(esp8266_at_parse_gateway(
              "noise+CIPSTA:gateway:\"1.2.3.4\"",
              output,
              (uint16_t)sizeof(output)) == -1);
    CHECK(strcmp(output, unchanged) == 0);

    memcpy(output, unchanged, sizeof(unchanged));
    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:junk\"1.2.3.4\"",
              output,
              (uint16_t)sizeof(output)) == -2);
    CHECK(strcmp(output, unchanged) == 0);

    memcpy(output, unchanged, sizeof(unchanged));
    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:192.168.137.1",
              output,
              (uint16_t)sizeof(output)) == -2);
    CHECK(strcmp(output, unchanged) == 0);

    memcpy(output, unchanged, sizeof(unchanged));
    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:\"\"",
              output,
              (uint16_t)sizeof(output)) == -2);
    CHECK(strcmp(output, unchanged) == 0);

    memcpy(output, unchanged, sizeof(unchanged));
    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:\"192.168.137.1",
              output,
              (uint16_t)sizeof(output)) == -2);
    CHECK(strcmp(output, unchanged) == 0);

    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:\"1.2.3.4\"",
              exact_output,
              (uint16_t)sizeof(exact_output)) == 0);
    CHECK(strcmp(exact_output, "1.2.3.4") == 0);

    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:\"1.2.3.4\"",
              short_output,
              (uint16_t)sizeof(short_output)) == -3);
    CHECK(strcmp(short_output, "same") == 0);

    memcpy(output, unchanged, sizeof(unchanged));
    CHECK(esp8266_at_parse_gateway(NULL, output,
                                   (uint16_t)sizeof(output)) == -2);
    CHECK(strcmp(output, unchanged) == 0);
    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:\"192.168.137.1\"",
              NULL,
              (uint16_t)sizeof(output)) == -2);
    CHECK(esp8266_at_parse_gateway(
              "+CIPSTA:gateway:\"192.168.137.1\"",
              output,
              0U) == -2);
    CHECK(strcmp(output, unchanged) == 0);

    return 0;
}
