# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

board_runner_args(jlink "--device=nrf5340_xxaa_app" "--speed=4000")
board_runner_args(jlink "--device=nrf5340_xxaa_net" "--speed=4000")
board_runner_args(pyocd "--target=nrf5340" "--frequency=4000000")

set(OPENOCD_NRF5_SUBFAMILY "nrf53")

if(CONFIG_TFM_FLASH_MERGED_BINARY)
  set_property(TARGET runners_yaml_props_target PROPERTY hex_file "${CMAKE_BINARY_DIR}/tfm_merged.hex")
endif()

include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd-nrf5.board.cmake)
