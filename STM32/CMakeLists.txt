cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
# 注：产物名（.elf/.bin）以此为名。原 BSP_demo 改名 Plant_Net 后同步；
#     若只想改回旧名，改这一行即可，不影响其它配置。
set(CMAKE_PROJECT_NAME Plant_Net)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# ============================================================================
# 用户代码层：Driver（板级驱动）+ BSP（外设集成选择器）+ App（业务）
# 分层（上层可依赖下层，反之不可）：
#   App ──> Driver ──> BSP ──> HAL
# 注意：本文件由 CubeMX 只生成一次，重新生成不会覆盖，可放心修改。
# 使用 GLOB_RECURSE + CONFIGURE_DEPENDS 自动收集各层 .c 文件：
# 新增源文件无需再改本文件，下次构建自动重新扫描（Ninja / Make 均支持）。
# ============================================================================
file(GLOB_RECURSE DRIVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Driver/*.c"
)
file(GLOB_RECURSE BSP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/BSP/Src/*.c"
)
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/App/Src/*.c"
)
file(GLOB_RECURSE MIDDLEWARE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Middleware/Src/*.c"
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${DRIVER_SOURCES}
    ${BSP_SOURCES}
    ${APP_SOURCES}
    ${MIDDLEWARE_SOURCES}
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Driver
    ${CMAKE_SOURCE_DIR}/BSP/Inc
    ${CMAKE_SOURCE_DIR}/App/Inc
    ${CMAKE_SOURCE_DIR}/Middleware/Inc
    ${CMAKE_SOURCE_DIR}
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
