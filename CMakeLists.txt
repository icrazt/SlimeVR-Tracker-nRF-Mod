#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

# Configure VERSION from repository branch and tag
find_package(Git)

if(GIT_EXECUTABLE)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} branch --show-current
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    )
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --dirty --tags --long --match [0-9]*.[0-9]*.[0-9]*
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    )
  # Prefer a https URL for display (falls back to ssh/origin.url)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} config --get remote.origin.url
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_REPO_URL_SSH
    )
  execute_process(
    COMMAND ${GIT_EXECUTABLE} config --get remote.origin.https.url
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_REPO_URL_HTTPS
    )
  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -1 --pretty=%an
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_AUTHOR
    )
endif()

if(GIT_BRANCH STREQUAL "")
  set(GIT_BRANCH unknown)
  message(WARNING "Unknown Git Branch")
endif()

if(GIT_VERSION STREQUAL "")
  set(GIT_VERSION 0.0.0-0)
  message(WARNING "Unknown Tag/Version")
endif()

# Choose URL: origin.https.url > origin.url
if(NOT GIT_REPO_URL_HTTPS STREQUAL "")
  set(GIT_REPO_URL ${GIT_REPO_URL_HTTPS})
else()
  set(GIT_REPO_URL ${GIT_REPO_URL_SSH})
endif()

# Normalize GitHub SSH URLs to HTTPS for display in firmware info
if(GIT_REPO_URL MATCHES "^git@github\\.com:(.+)$")
  set(GIT_REPO_URL "https://github.com/${CMAKE_MATCH_1}")
elseif(GIT_REPO_URL MATCHES "^ssh://git@github\\.com/(.+)$")
  set(GIT_REPO_URL "https://github.com/${CMAKE_MATCH_1}")
endif()
string(REGEX REPLACE "\\.git$" "" GIT_REPO_URL "${GIT_REPO_URL}")

if(GIT_REPO_URL STREQUAL "")
  set(GIT_REPO_URL unknown)
  message(WARNING "Unknown Git Repo URL")
endif()

if(GIT_AUTHOR STREQUAL "")
  set(GIT_AUTHOR unknown)
  message(WARNING "Unknown Git Author")
endif()

string(STRIP ${GIT_BRANCH} GIT_BRANCH)
string(STRIP ${GIT_VERSION} GIT_VERSION)
string(STRIP ${GIT_REPO_URL} GIT_REPO_URL)
string(STRIP ${GIT_AUTHOR} GIT_AUTHOR)

string(REPLACE "-" ";" GIT_VERSION ${GIT_VERSION})
list(GET GIT_VERSION 0 GIT_RELEASE)
list(GET GIT_VERSION 1 GIT_TWEAK)
string(REPLACE "v" "" GIT_RELEASE ${GIT_RELEASE})
string(REPLACE "." ";" GIT_RELEASE ${GIT_RELEASE})
list(GET GIT_RELEASE 0 GIT_MAJOR)
list(GET GIT_RELEASE 1 GIT_MINOR)
list(GET GIT_RELEASE 2 GIT_PATCH)

configure_file(${CMAKE_SOURCE_DIR}/VERSION_SRC ${CMAKE_SOURCE_DIR}/VERSION @ONLY)

# Pass metadata to C compilation units
add_compile_definitions(
  FW_GIT_BRANCH="${GIT_BRANCH}"
  FW_GIT_REPO_URL="${GIT_REPO_URL}"
  FW_GIT_AUTHOR="${GIT_AUTHOR}"
)

# Zephyr
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

find_package(Python3 QUIET COMPONENTS Interpreter)

if(NOT Python3_Interpreter_FOUND)
  find_program(PYTHON_EXECUTABLE NAMES python3 python REQUIRED)
  set(Python3_EXECUTABLE ${PYTHON_EXECUTABLE})
endif()

set(SOFTDEVICE_HEX_52840 ${CMAKE_SOURCE_DIR}/softdevice/s140_nrf52_7.3.0_softdevice.hex)
set(UF2_FAMILY_ID_52840 0xADA52840)
set(ADAFRUIT_BOOTLOADER_HEX "" CACHE FILEPATH "Optional Adafruit bootloader hex to merge into a factory-programmable image")

if(WITH_SOFTDEVICE)
  if(CONFIG_SOC_NRF52840)
    set(sd_softdevice_hex ${SOFTDEVICE_HEX_52840})
    set(sd_uf2_family_id ${UF2_FAMILY_ID_52840})
  else()
    message(FATAL_ERROR "WITH_SOFTDEVICE is currently only supported on nRF52840 targets")
  endif()

  if(DEFINED sd_softdevice_hex)
    if(NOT EXISTS ${sd_softdevice_hex})
      message(FATAL_ERROR "SoftDevice hex not found: ${sd_softdevice_hex}")
    endif()

    set(sd_merged_hex ${CMAKE_BINARY_DIR}/zephyr/zephyr_sd.hex)
    set(sd_merged_uf2 ${CMAKE_BINARY_DIR}/zephyr/zephyr_sd.uf2)

    add_custom_command(
      OUTPUT ${sd_merged_hex} ${sd_merged_uf2}
      COMMAND ${Python3_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/mergehex.py --overlap=replace -o ${sd_merged_hex} ${sd_softdevice_hex} ${CMAKE_BINARY_DIR}/zephyr/zephyr.hex
      COMMAND ${Python3_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/uf2conv.py ${sd_merged_hex} -c -f ${sd_uf2_family_id} -o ${sd_merged_uf2}
      DEPENDS ${sd_softdevice_hex} ${CMAKE_BINARY_DIR}/zephyr/zephyr.hex
      VERBATIM
    )

    add_custom_target(sd_merged_uf2 ALL DEPENDS ${sd_merged_hex} ${sd_merged_uf2})
  endif()
endif()

if(CONFIG_BUILD_OUTPUT_UF2)
  if(CONFIG_SOC_NRF52840)
    set(adafruit_settings_address 0xFF000)
    set(adafruit_mbr_params_address 0xFE000)
  elseif(CONFIG_SOC_NRF52833)
    set(adafruit_settings_address 0x7F000)
    set(adafruit_mbr_params_address 0x7E000)
  endif()

  if(DEFINED adafruit_settings_address)
    set(app_image_hex ${CMAKE_BINARY_DIR}/zephyr/zephyr.hex)
    set(swd_base_hex ${app_image_hex})
    if(WITH_SOFTDEVICE)
      set(swd_base_hex ${CMAKE_BINARY_DIR}/zephyr/zephyr_sd.hex)
    endif()

    set(adafruit_pages_hex ${CMAKE_BINARY_DIR}/zephyr/adafruit_bootloader_pages.hex)
    set(swd_ready_hex ${CMAKE_BINARY_DIR}/zephyr/zephyr_swd.hex)
    set(factory_ready_hex ${CMAKE_BINARY_DIR}/zephyr/zephyr_factory.hex)

    add_custom_command(
      OUTPUT ${adafruit_pages_hex} ${swd_ready_hex}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/build/gen_adafruit_bootloader_pages.py
              --app-hex ${app_image_hex}
              --output-hex ${adafruit_pages_hex}
              --app-start ${CONFIG_FLASH_LOAD_OFFSET}
              --settings-address ${adafruit_settings_address}
              --mbr-params-address ${adafruit_mbr_params_address}
      COMMAND ${Python3_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/mergehex.py --overlap=replace -o ${swd_ready_hex} ${swd_base_hex} ${adafruit_pages_hex}
      DEPENDS
              ${app_image_hex}
              ${swd_base_hex}
              ${CMAKE_SOURCE_DIR}/scripts/build/gen_adafruit_bootloader_pages.py
      VERBATIM
    )

    # Advertise the SWD-ready image as the preferred hex byproduct so sysbuild
    # merges and flashes an image the Adafruit bootloader will accept.
    set(BYPRODUCT_KERNEL_SIGNED_HEX_NAME "${swd_ready_hex}"
        CACHE FILEPATH "SWD-ready kernel hex file" FORCE
    )

    add_custom_target(swd_ready_hex ALL DEPENDS ${adafruit_pages_hex} ${swd_ready_hex})

    if(ADAFRUIT_BOOTLOADER_HEX AND EXISTS ${ADAFRUIT_BOOTLOADER_HEX})
      add_custom_command(
        OUTPUT ${factory_ready_hex}
        COMMAND ${Python3_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/mergehex.py --overlap=replace -o ${factory_ready_hex} ${ADAFRUIT_BOOTLOADER_HEX} ${swd_ready_hex}
        DEPENDS ${swd_ready_hex} ${ADAFRUIT_BOOTLOADER_HEX}
        VERBATIM
      )

      add_custom_target(factory_ready_hex ALL DEPENDS ${factory_ready_hex})
    endif()
  endif()
endif()

# NORDIC SDK APP START
FILE(GLOB_RECURSE app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# NORDIC SDK APP END

FILE(GLOB_RECURSE files Fusion/Fusion/*.c)
target_sources(app PRIVATE ${files})

FILE(GLOB_RECURSE files vqf-c/src/*.c)
target_sources(app PRIVATE ${files})

target_include_directories(app PRIVATE src)
target_include_directories(app PRIVATE Fusion/Fusion)
target_include_directories(app PRIVATE vqf-c/src)

# Force recompile for updated build timestamp
add_custom_target(touch_util_h ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_SOURCE_DIR}/src/build_defines.h
)
