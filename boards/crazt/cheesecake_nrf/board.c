/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/init.h>

#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#define SYSOFF_GPIO_PIN DT_GPIO_PIN(ZEPHYR_USER_NODE, sysoff_gpios)
#define SYSOFF_GPIO_PORT_NUM DT_PROP(DT_GPIO_CTLR(ZEPHYR_USER_NODE, sysoff_gpios), port)
#define SYSOFF_GPIO NRF_GPIO_PIN_MAP(SYSOFF_GPIO_PORT_NUM, SYSOFF_GPIO_PIN)

static void board_sysoff_hold_low(void)
{
	/*
	 * SYSOFF is weakly pulled up to VBAT by the external power path. Clamp
	 * it low as soon as firmware runs so reset/SYSTEM_OFF recovery cannot
	 * briefly request battery cutoff while REG0 is ramping.
	 */
	nrf_gpio_pin_clear(SYSOFF_GPIO);
	nrf_gpio_cfg(
		SYSOFF_GPIO,
		NRF_GPIO_PIN_DIR_OUTPUT,
		NRF_GPIO_PIN_INPUT_DISCONNECT,
		NRF_GPIO_PIN_NOPULL,
		NRF_GPIO_PIN_H0S1,
		NRF_GPIO_PIN_NOSENSE
	);
	nrf_gpio_pin_clear(SYSOFF_GPIO);
}

void board_early_init_hook(void)
{
	board_sysoff_hold_low();

	if ((nrf_power_mainregstatus_get(NRF_POWER) == NRF_POWER_MAINREGSTATUS_HIGH) &&
	    ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk) ==
	     (UICR_REGOUT0_VOUT_DEFAULT << UICR_REGOUT0_VOUT_Pos))) {
		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
			__NOP();
		}

		NRF_UICR->REGOUT0 =
			(NRF_UICR->REGOUT0 & ~((uint32_t)UICR_REGOUT0_VOUT_Msk)) |
			(UICR_REGOUT0_VOUT_2V7 << UICR_REGOUT0_VOUT_Pos);

		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
			__NOP();
		}

		NVIC_SystemReset();
	}
}

static int board_cheesecake_nrf_init(void)
{
	board_sysoff_hold_low();
	return 0;
}

SYS_INIT(board_cheesecake_nrf_init, PRE_KERNEL_1, 0);
