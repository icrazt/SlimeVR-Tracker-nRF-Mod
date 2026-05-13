/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/init.h>

#include <hal/nrf_gpio.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#define SYSOFF_GPIO_PIN DT_GPIO_PIN(ZEPHYR_USER_NODE, sysoff_gpios)
#define SYSOFF_GPIO_PORT_NUM DT_PROP(DT_GPIO_CTLR(ZEPHYR_USER_NODE, sysoff_gpios), port)
#define SYSOFF_GPIO NRF_GPIO_PIN_MAP(SYSOFF_GPIO_PORT_NUM, SYSOFF_GPIO_PIN)

static int board_cheesecake_nrf_init(void)
{
	/*
	 * P1.13 is active-high for external power cutoff. Keep it inactive/low
	 * during normal operation and through nRF System OFF GPIO retention.
	 */
	nrf_gpio_pin_clear(SYSOFF_GPIO);
	nrf_gpio_cfg(
		SYSOFF_GPIO,
		NRF_GPIO_PIN_DIR_OUTPUT,
		NRF_GPIO_PIN_INPUT_DISCONNECT,
		NRF_GPIO_PIN_NOPULL,
		NRF_GPIO_PIN_S0S1,
		NRF_GPIO_PIN_NOSENSE
	);

	return 0;
}

SYS_INIT(board_cheesecake_nrf_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
