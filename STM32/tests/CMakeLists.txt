cmake_minimum_required(VERSION 3.16)

project(esp8266_at_parse_tests C)

add_executable(test_esp8266_at_parse
    test_esp8266_at_parse.c
    ../Driver/ESP8266_AT_Parse.c
)

target_include_directories(test_esp8266_at_parse PRIVATE ../Driver)
target_compile_features(test_esp8266_at_parse PRIVATE c_std_11)

if(MSVC)
    target_compile_options(test_esp8266_at_parse PRIVATE /W4 /WX)
else()
    target_compile_options(test_esp8266_at_parse PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME esp8266_at_parse COMMAND test_esp8266_at_parse)
