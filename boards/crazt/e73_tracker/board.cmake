# Copyright (c) 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

set(e73_tracker_uf2_variant FALSE)
if(DEFINED BOARD_QUALIFIERS AND BOARD_QUALIFIERS MATCHES "(^|/)uf2($|/)")
  set(e73_tracker_uf2_variant TRUE)
endif()

board_runner_args(jlink "--device=nrf52840_xxaa" "--speed=4000")

if(e73_tracker_uf2_variant)
  board_runner_args(pyocd "--target=nrf52840" "--frequency=4000000")
  include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
endif()
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
if(e73_tracker_uf2_variant)
  include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
endif()
