/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT slimevr_ws2812_spi_packed

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/dt-bindings/led/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ws2812_spi_packed, CONFIG_LED_STRIP_LOG_LEVEL);

#define WS2812_SPI_WORD_BITS 8U
#define WS2812_CHANNEL_BITS 8U

struct ws2812_spi_packed_cfg {
	struct spi_dt_spec bus;
	uint8_t *tx_buf;
	size_t reset_bytes;
	size_t bytes_per_pixel;
	size_t length;
	uint8_t bits_per_symbol;
	uint8_t one_frame;
	uint8_t zero_frame;
	uint8_t num_colors;
	const uint8_t *color_mapping;
};

static const struct ws2812_spi_packed_cfg *dev_cfg(const struct device *dev)
{
	return dev->config;
}

static inline void ws2812_spi_packed_put_bit(uint8_t *buf, size_t bit_pos, bool value)
{
	uint8_t mask = BIT(7U - (bit_pos % WS2812_SPI_WORD_BITS));

	if (value) {
		buf[bit_pos / WS2812_SPI_WORD_BITS] |= mask;
	} else {
		buf[bit_pos / WS2812_SPI_WORD_BITS] &= ~mask;
	}
}

static void ws2812_spi_packed_put_symbol(
	uint8_t *buf,
	size_t *bit_pos,
	uint8_t symbol,
	uint8_t bits_per_symbol)
{
	for (int bit = bits_per_symbol - 1; bit >= 0; bit--) {
		ws2812_spi_packed_put_bit(buf, (*bit_pos)++, (symbol & BIT(bit)) != 0);
	}
}

static void ws2812_spi_packed_ser(
	uint8_t *buf,
	size_t *bit_pos,
	uint8_t color,
	const struct ws2812_spi_packed_cfg *cfg)
{
	for (uint8_t bit = 0; bit < WS2812_CHANNEL_BITS; bit++) {
		uint8_t symbol = (color & BIT(7U - bit)) ? cfg->one_frame : cfg->zero_frame;

		ws2812_spi_packed_put_symbol(buf, bit_pos, symbol, cfg->bits_per_symbol);
	}
}

static int ws2812_spi_packed_update_rgb(
	const struct device *dev,
	struct led_rgb *pixels,
	size_t num_pixels)
{
	const struct ws2812_spi_packed_cfg *cfg = dev_cfg(dev);
	size_t transfer_len;
	size_t bit_pos;

	if (num_pixels > cfg->length) {
		return -ERANGE;
	}

	transfer_len = (2U * cfg->reset_bytes) + (num_pixels * cfg->bytes_per_pixel);
	memset(cfg->tx_buf, 0xff, transfer_len);
	bit_pos = cfg->reset_bytes * WS2812_SPI_WORD_BITS;

	for (size_t i = 0; i < num_pixels; i++) {
		for (uint8_t j = 0; j < cfg->num_colors; j++) {
			uint8_t channel;

			switch (cfg->color_mapping[j]) {
			case LED_COLOR_ID_RED:
				channel = pixels[i].r;
				break;
			case LED_COLOR_ID_GREEN:
				channel = pixels[i].g;
				break;
			case LED_COLOR_ID_BLUE:
				channel = pixels[i].b;
				break;
			case LED_COLOR_ID_WHITE:
				channel = 0;
				break;
			default:
				return -EINVAL;
			}

			ws2812_spi_packed_ser(cfg->tx_buf, &bit_pos, channel, cfg);
		}
	}

	struct spi_buf buf = {
		.buf = cfg->tx_buf,
		.len = transfer_len,
	};
	const struct spi_buf_set tx = {
		.buffers = &buf,
		.count = 1,
	};

	return spi_write_dt(&cfg->bus, &tx);
}

static size_t ws2812_spi_packed_length(const struct device *dev)
{
	const struct ws2812_spi_packed_cfg *cfg = dev_cfg(dev);

	return cfg->length;
}

static int ws2812_spi_packed_init(const struct device *dev)
{
	const struct ws2812_spi_packed_cfg *cfg = dev_cfg(dev);

	if (!spi_is_ready_dt(&cfg->bus)) {
		LOG_ERR("SPI device %s not ready", cfg->bus.bus->name);
		return -ENODEV;
	}

	for (uint8_t i = 0U; i < cfg->num_colors; i++) {
		switch (cfg->color_mapping[i]) {
		case LED_COLOR_ID_WHITE:
		case LED_COLOR_ID_RED:
		case LED_COLOR_ID_GREEN:
		case LED_COLOR_ID_BLUE:
			break;
		default:
			LOG_ERR("%s: invalid channel to color mapping", dev->name);
			return -EINVAL;
		}
	}

	return 0;
}

static DEVICE_API(led_strip, ws2812_spi_packed_api) = {
	.update_rgb = ws2812_spi_packed_update_rgb,
	.length = ws2812_spi_packed_length,
};

#define WS2812_SPI_PACKED_BITS(idx) DT_INST_PROP(idx, bits_per_symbol)
#define WS2812_SPI_PACKED_NUM_COLORS(idx) DT_INST_PROP_LEN(idx, color_mapping)
#define WS2812_SPI_PACKED_DATA_BITS(idx)                                                    \
	(DT_INST_PROP(idx, chain_length) * WS2812_SPI_PACKED_NUM_COLORS(idx) *              \
	 WS2812_CHANNEL_BITS * WS2812_SPI_PACKED_BITS(idx))
#define WS2812_SPI_PACKED_DATA_BYTES(idx)                                                   \
	(WS2812_SPI_PACKED_DATA_BITS(idx) / WS2812_SPI_WORD_BITS)
#define WS2812_SPI_PACKED_BYTES_PER_PIXEL(idx)                                              \
	((WS2812_SPI_PACKED_NUM_COLORS(idx) * WS2812_CHANNEL_BITS *                         \
	  WS2812_SPI_PACKED_BITS(idx)) /                                                     \
	 WS2812_SPI_WORD_BITS)
#define WS2812_SPI_PACKED_RESET_BYTES(idx)                                                  \
	DIV_ROUND_UP(                                                                       \
		DT_INST_PROP(idx, reset_delay) * (DT_INST_PROP(idx, spi_max_frequency) / 1000U),\
		WS2812_SPI_WORD_BITS * 1000U)
#define WS2812_SPI_PACKED_TX_BYTES(idx)                                                     \
	(WS2812_SPI_PACKED_DATA_BYTES(idx) + (2U * WS2812_SPI_PACKED_RESET_BYTES(idx)))

#define WS2812_SPI_PACKED_COLOR_MAPPING(idx)                                                \
	static const uint8_t ws2812_spi_packed_##idx##_color_mapping[] =                    \
		DT_INST_PROP(idx, color_mapping)

#define WS2812_SPI_PACKED_DEVICE(idx)                                                       \
	BUILD_ASSERT(WS2812_SPI_PACKED_BITS(idx) == 5,                                      \
		     "slimevr,ws2812-spi-packed currently supports only 5-bit symbols");     \
	BUILD_ASSERT(DT_INST_PROP(idx, spi_max_frequency) == 4000000,                       \
		     "slimevr,ws2812-spi-packed expects 4 MHz SPI for 1.25 us symbols");     \
	BUILD_ASSERT((WS2812_SPI_PACKED_DATA_BITS(idx) % WS2812_SPI_WORD_BITS) == 0,        \
		     "packed WS2812 data must end on a whole SPI byte");                    \
	BUILD_ASSERT(DT_INST_PROP(idx, spi_one_frame) < BIT(WS2812_SPI_PACKED_BITS(idx)),   \
		     "spi-one-frame does not fit in bits-per-symbol");                      \
	BUILD_ASSERT(DT_INST_PROP(idx, spi_zero_frame) < BIT(WS2812_SPI_PACKED_BITS(idx)),  \
		     "spi-zero-frame does not fit in bits-per-symbol");                     \
	static uint8_t ws2812_spi_packed_##idx##_tx_buf[WS2812_SPI_PACKED_TX_BYTES(idx)];   \
	WS2812_SPI_PACKED_COLOR_MAPPING(idx);                                               \
	static const struct ws2812_spi_packed_cfg ws2812_spi_packed_##idx##_cfg = {          \
		.bus = SPI_DT_SPEC_INST_GET(                                                \
			idx, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0),   \
		.tx_buf = ws2812_spi_packed_##idx##_tx_buf,                                \
		.reset_bytes = WS2812_SPI_PACKED_RESET_BYTES(idx),                          \
		.bytes_per_pixel = WS2812_SPI_PACKED_BYTES_PER_PIXEL(idx),                  \
		.length = DT_INST_PROP(idx, chain_length),                                  \
		.bits_per_symbol = WS2812_SPI_PACKED_BITS(idx),                             \
		.one_frame = DT_INST_PROP(idx, spi_one_frame),                              \
		.zero_frame = DT_INST_PROP(idx, spi_zero_frame),                            \
		.num_colors = WS2812_SPI_PACKED_NUM_COLORS(idx),                            \
		.color_mapping = ws2812_spi_packed_##idx##_color_mapping,                   \
	};                                                                                   \
	DEVICE_DT_INST_DEFINE(                                                               \
		idx, ws2812_spi_packed_init, NULL, NULL, &ws2812_spi_packed_##idx##_cfg,     \
		POST_KERNEL, CONFIG_LED_STRIP_INIT_PRIORITY, &ws2812_spi_packed_api);

DT_INST_FOREACH_STATUS_OKAY(WS2812_SPI_PACKED_DEVICE)
