#include "globals.h"

#include <math.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include "led.h"

LOG_MODULE_REGISTER(led, LOG_LEVEL_INF);

static void led_thread(void);
/* Keep ws2812_i2s START/DRAIN from being preempted by connection TX. */
K_THREAD_DEFINE(led_thread_id, 512, led_thread, NULL, NULL, NULL, 4, 0, 0);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, led_en_gpios)
#define LED_EN_EXISTS true
static const struct gpio_dt_spec led_en = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, led_en_gpios);
#endif

#if CONFIG_LED_STRIP
#define LED_STRIP_EXISTS true
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/led_strip.h>
#define STRIP_NODE DT_ALIAS(led_strip)
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static const struct device *const strip_i2s = DEVICE_DT_GET(DT_BUS(STRIP_NODE));
#endif

#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, led_gpios)
#define LED_EXISTS true
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, led_gpios);
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led0))
#ifndef LED_EXISTS
#define LED_EXISTS true
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#else
#define LED0_EXISTS true
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif
#endif
#ifndef LED_EXISTS
#ifndef LED_STRIP_EXISTS
#warning "LED GPIO does not exist"
// static const struct gpio_dt_spec led = {0};
#endif
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
#define LED1_EXISTS true
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
#define LED2_EXISTS true
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
#define LED3_EXISTS true
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);
#endif

#if DT_NODE_EXISTS(DT_ALIAS(pwm_led0))
#define PWM_LED_EXISTS true
static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
#else
#ifndef LED_STRIP_EXISTS
#warning "PWM LED node does not exist"
#endif
#endif
#if DT_NODE_EXISTS(DT_ALIAS(pwm_led1))
#define PWM_LED1_EXISTS true
static const struct pwm_dt_spec pwm_led1 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1));
#endif
#if DT_NODE_EXISTS(DT_ALIAS(pwm_led2))
#define PWM_LED2_EXISTS true
static const struct pwm_dt_spec pwm_led2 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));
#endif

static enum sys_led_pattern current_led_pattern;
static int current_priority;
static bool led_output_active;

#if LED_EXISTS || LED_STRIP_EXISTS
static enum sys_led_pattern led_patterns[SYS_LED_PATTERN_DEPTH]
	= {[0 ...(SYS_LED_PATTERN_DEPTH - 1)] = SYS_LED_PATTERN_OFF};
static int led_pattern_state;

#define LED_PULSE_CURVE_HALF_STEPS 500
#define LED_PULSE_STEPS (CONFIG_LED_PULSE_PERIOD_MS / CONFIG_LED_PULSE_UPDATE_MS)
#define LED_PULSE_HALF_STEPS (LED_PULSE_STEPS / 2)

#if CONFIG_LED_PULSE_PERIOD_MS < (2 * CONFIG_LED_PULSE_UPDATE_MS)
#error "CONFIG_LED_PULSE_PERIOD_MS must be at least twice CONFIG_LED_PULSE_UPDATE_MS"
#endif

static int led_pin_init(void)
{
	LOG_DBG("led_pin_init");
#if LED_EXISTS
	gpio_pin_configure_dt(&led, GPIO_OUTPUT);
	gpio_pin_set_dt(&led, 0);
#endif
#if LED0_EXISTS
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT);
	gpio_pin_set_dt(&led0, 0);
#endif
#if LED1_EXISTS
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT);
	gpio_pin_set_dt(&led1, 0);
#endif
#if LED2_EXISTS
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT);
	gpio_pin_set_dt(&led2, 0);
#endif
#if LED3_EXISTS
	gpio_pin_configure_dt(&led3, GPIO_OUTPUT);
	gpio_pin_set_dt(&led3, 0);
#endif
	return 0;
}

SYS_INIT(led_pin_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static void led_pin_reset(void)
{
	LOG_DBG("led_pin_reset");
#if LED_EXISTS
	gpio_pin_configure_dt(&led, GPIO_DISCONNECTED);
#endif
#if LED0_EXISTS
	gpio_pin_configure_dt(&led0, GPIO_DISCONNECTED);
#endif
#if LED1_EXISTS
	gpio_pin_configure_dt(&led1, GPIO_DISCONNECTED);
#endif
#if LED2_EXISTS
	gpio_pin_configure_dt(&led2, GPIO_DISCONNECTED);
#endif
#if LED3_EXISTS
	gpio_pin_configure_dt(&led3, GPIO_DISCONNECTED);
#endif
}

#ifdef LED_STRIP_EXISTS
#define LED_STRIP_ERROR_BACKOFF_MS 100

static int led_strip_update_rgb_checked(struct led_rgb *pixels, size_t num_pixels)
{
	static int64_t retry_after;
	int64_t now = k_uptime_get();
	int ret;

	if (now < retry_after) {
		return -EAGAIN;
	}

	ret = led_strip_update_rgb(strip, pixels, num_pixels);
	if (ret == -EIO && i2s_trigger(strip_i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE) == 0) {
		ret = led_strip_update_rgb(strip, pixels, num_pixels);
	}

	retry_after = ret ? now + LED_STRIP_ERROR_BACKOFF_MS : 0;
	return ret;
}

static void led_strip_clear(void)
{
	static struct led_rgb pixels[DT_PROP(STRIP_NODE, chain_length)];
	int ret = led_strip_update_rgb_checked(pixels, DT_PROP(STRIP_NODE, chain_length));

	if (ret && ret != -EAGAIN) {
		LOG_WRN("Failed to clear LED strip: %d", ret);
	}
}
#endif

static void led_suspend(void)
{
	LOG_DBG("led_suspend");
	if (led_output_active) {
#ifdef LED_STRIP_EXISTS
		led_strip_clear();
		pm_device_action_run(strip, PM_DEVICE_ACTION_SUSPEND);
#endif
#ifdef PWM_LED_EXISTS
		pm_device_action_run(pwm_led.dev, PM_DEVICE_ACTION_SUSPEND);
#endif
#ifdef PWM_LED1_EXISTS
		pm_device_action_run(pwm_led1.dev, PM_DEVICE_ACTION_SUSPEND);
#endif
#ifdef PWM_LED2_EXISTS
		pm_device_action_run(pwm_led2.dev, PM_DEVICE_ACTION_SUSPEND);
#endif
		led_pin_reset();
		led_output_active = false;
	}
	// disable power
#if LED_EN_EXISTS
	gpio_pin_configure_dt(&led_en, GPIO_OUTPUT);
	gpio_pin_set_dt(&led_en, 0);
#endif
}

static void led_resume(void)
{
	LOG_DBG("led_resume");
	// enable power
#if LED_EN_EXISTS
	gpio_pin_configure_dt(&led_en, GPIO_OUTPUT);
	gpio_pin_set_dt(&led_en, 1);
#endif
#ifdef LED_STRIP_EXISTS
	pm_device_action_run(strip, PM_DEVICE_ACTION_RESUME);
#endif
#ifdef PWM_LED_EXISTS
	pm_device_action_run(pwm_led.dev, PM_DEVICE_ACTION_RESUME);
#endif
#ifdef PWM_LED1_EXISTS
	pm_device_action_run(pwm_led1.dev, PM_DEVICE_ACTION_RESUME);
#endif
#ifdef PWM_LED2_EXISTS
	pm_device_action_run(pwm_led2.dev, PM_DEVICE_ACTION_RESUME);
#endif
	led_pin_init();
	led_output_active = true;
}

#ifdef LED_STRIP_EXISTS
#define LED_RGB_COLOR
#else
#ifdef CONFIG_LED_RGB_COLOR
#define LED_RGB_COLOR
#define LED_RG_COLOR
#endif

#if PWM_LED_EXISTS && PWM_LED1_EXISTS && PWM_LED2_EXISTS
#define LED_TRI_COLOR
#else
#undef LED_RGB_COLOR
#undef LED_TRI_COLOR
#if PWM_LED_EXISTS && PWM_LED1_EXISTS
#define LED_DUAL_COLOR
#else
#undef LED_RG_COLOR
#undef LED_DUAL_COLOR
#endif
#endif
#endif

#ifdef LED_RGB_COLOR
static int led_pwm_period[5][3] = {
	{CONFIG_LED_DEFAULT_COLOR_R, CONFIG_LED_DEFAULT_COLOR_G, CONFIG_LED_DEFAULT_COLOR_B}, // Default
	{0, 10000, 0},                                                                        // Success
	{10000, 0, 0},                                                                        // Error
	{8000, 2000, 0},                                                                      // Charging
	{0, 0, 10000},                                                                        // Pairing
};
#elif defined(LED_TRI_COLOR)
static int led_pwm_period[5][3] = {
	{0, 0, 10000},   // Default
	{0, 10000, 0},   // Success
	{10000, 0, 0},   // Error
	{6000, 4000, 0}, // Charging
	{0, 0, 10000},   // Pairing
};
#elif defined(LED_RG_COLOR)
static int led_pwm_period[5][2] = {
	{CONFIG_LED_DEFAULT_COLOR_R, CONFIG_LED_DEFAULT_COLOR_G}, // Default
	{0, 10000},                                               // Success
	{10000, 0},                                               // Error
	{8000, 2000},                                             // Charging
	{4000, 6000},                                             // Pairing
};
#elif defined(LED_DUAL_COLOR)
static int led_pwm_period[5][2] = {
	{0, 10000},   // Default
	{0, 10000},   // Success
	{10000, 0},   // Error
	{6000, 4000}, // Charging
	{0, 10000},   // Pairing
};
#else
static int led_pwm_period[5][1] = {
	{10000}, // Default
	{10000}, // Success
	{10000}, // Error
	{10000}, // Charging
	{10000}, // Pairing
};
#endif

// Using brightness and value if PWM is supported, otherwise value is coerced to on/off
// TODO: use computed constants for high/low brightness and color values
static void led_pin_set(enum sys_led_color color, int brightness_pptt, int value_pptt)
{
	LOG_DBG("led_pin_set: color %d, brightness %d, value %d", color, brightness_pptt, value_pptt);
	if (brightness_pptt < 0) {
		brightness_pptt = 0;
	} else if (brightness_pptt > 10000) {
		brightness_pptt = 10000;
	}
	if (value_pptt < 0) {
		value_pptt = 0;
	} else if (value_pptt > 10000) {
		value_pptt = 10000;
	}
#if LED_STRIP_EXISTS
	static struct led_rgb pixel[1];
	value_pptt = value_pptt * brightness_pptt / 10000;
	pixel[0].r = 255 * (led_pwm_period[color][0] * value_pptt / 10000) / 10000;
	pixel[0].g = 255 * (led_pwm_period[color][1] * value_pptt / 10000) / 10000;
	pixel[0].b = 255 * (led_pwm_period[color][2] * value_pptt / 10000) / 10000;
	led_strip_update_rgb_checked(pixel, 1);
#elif PWM_LED_EXISTS
	value_pptt = value_pptt * brightness_pptt / 10000;
	// only supporting color if PWM is supported
	pwm_set_pulse_dt(&pwm_led, pwm_led.period / 10000 * (led_pwm_period[color][0] * value_pptt / 10000));
#if PWM_LED1_EXISTS
	pwm_set_pulse_dt(&pwm_led1, pwm_led1.period / 10000 * (led_pwm_period[color][1] * value_pptt / 10000));
#if PWM_LED2_EXISTS
	pwm_set_pulse_dt(&pwm_led2, pwm_led2.period / 10000 * (led_pwm_period[color][2] * value_pptt / 10000));
#endif
#endif
#else
	gpio_pin_set_dt(&led, value_pptt > 5000);
#endif
}
#endif

static const char *led_pattern_name(enum sys_led_pattern pattern)
{
	switch (pattern) {
	case SYS_LED_PATTERN_OFF_FORCE:
		return "OFF_FORCE";
	case SYS_LED_PATTERN_OFF:
		return "OFF";
	case SYS_LED_PATTERN_ON:
		return "ON";
	case SYS_LED_PATTERN_SHORT:
		return "SHORT";
	case SYS_LED_PATTERN_LONG:
		return "LONG";
	case SYS_LED_PATTERN_FLASH:
		return "FLASH";
	case SYS_LED_PATTERN_ONESHOT_POWERON:
		return "ONESHOT_POWERON";
	case SYS_LED_PATTERN_ONESHOT_POWEROFF:
		return "ONESHOT_POWEROFF";
	case SYS_LED_PATTERN_ONESHOT_PROGRESS:
		return "ONESHOT_PROGRESS";
	case SYS_LED_PATTERN_ONESHOT_COMPLETE:
		return "ONESHOT_COMPLETE";
	case SYS_LED_PATTERN_ONESHOT_PING:
		return "ONESHOT_PING";
	case SYS_LED_PATTERN_ON_PERSIST:
		return "ON_PERSIST";
	case SYS_LED_PATTERN_LONG_PERSIST:
		return "LONG_PERSIST";
	case SYS_LED_PATTERN_PULSE_PERSIST:
		return "PULSE_PERSIST";
	case SYS_LED_PATTERN_ACTIVE_PERSIST:
		return "ACTIVE_PERSIST";
	case SYS_LED_PATTERN_ERROR_A:
		return "ERROR_A";
	case SYS_LED_PATTERN_ERROR_B:
		return "ERROR_B";
	case SYS_LED_PATTERN_ERROR_C:
		return "ERROR_C";
	case SYS_LED_PATTERN_ERROR_D:
		return "ERROR_D";
	default:
		return "UNKNOWN";
	}
}

static const char *led_priority_name(int priority)
{
	switch (priority) {
	case SYS_LED_PRIORITY_HIGHEST:
		return "highest/boot";
	case SYS_LED_PRIORITY_SENSOR:
		return "sensor";
	case SYS_LED_PRIORITY_CONNECTION:
		return "connection";
	case SYS_LED_PRIORITY_STATUS:
		return "status";
	case SYS_LED_PRIORITY_SYSTEM:
		return "system";
	case SYS_LED_PATTERN_DEPTH:
		return "none";
	default:
		return "unknown";
	}
}

static const char *led_source_basename(const char *source_file)
{
	const char *base = source_file ? source_file : "?";

	if (!source_file) {
		return base;
	}

	for (const char *p = source_file; *p != '\0'; p++) {
		if (*p == '/' || *p == '\\') {
			base = p + 1;
		}
	}

	return base;
}

void set_led_with_context(
	enum sys_led_pattern led_pattern,
	int priority,
	const char *source_file,
	int source_line,
	const char *source_func
)
{
	const enum sys_led_pattern requested_pattern = led_pattern;
	const int requested_priority = priority;
	const char *source_name = led_source_basename(source_file);
	const char *func_name = source_func ? source_func : "?";

	LOG_DBG("set_led: current_led_pattern %d, current_priority %d", current_led_pattern, current_priority);
	LOG_DBG("set_led: pattern %d, priority %d", led_pattern, priority);
#if LED_EXISTS || LED_STRIP_EXISTS
	if (priority < 0 || priority >= SYS_LED_PATTERN_DEPTH) {
		LOG_WRN(
			"LED request ignored: %s priority=%d from %s:%d %s()",
			led_pattern_name(led_pattern),
			priority,
			source_name,
			source_line,
			func_name
		);
		return;
	}

	int slot_priority = priority;
	if (led_pattern <= SYS_LED_PATTERN_OFF && k_current_get() == led_thread_id &&
	    current_priority >= 0 && current_priority < SYS_LED_PATTERN_DEPTH) {
		slot_priority = current_priority;
	}

	enum sys_led_pattern previous_slot_pattern = led_patterns[slot_priority];
	led_patterns[slot_priority] = led_pattern;
	if (previous_slot_pattern != led_pattern) {
		LOG_INF(
			"LED slot %s[%d]: %s -> %s from %s:%d %s()",
			led_priority_name(slot_priority),
			slot_priority,
			led_pattern_name(previous_slot_pattern),
			led_pattern_name(led_pattern),
			source_name,
			source_line,
			func_name
		);
	}

	for (priority = 0; priority < SYS_LED_PATTERN_DEPTH; priority++) {
		if (led_patterns[priority] == SYS_LED_PATTERN_OFF) {
			continue;
		}
		led_pattern = led_patterns[priority];
		break;
	}
	if (priority == SYS_LED_PATTERN_DEPTH) {
		led_pattern = SYS_LED_PATTERN_OFF;
	}
	if (led_pattern == current_led_pattern && led_pattern > SYS_LED_PATTERN_OFF) {
		return;
	}

	enum sys_led_pattern previous_pattern = current_led_pattern;
	int previous_priority = current_priority;
	current_led_pattern = led_pattern;
	current_priority = priority;
	led_pattern_state = 0;
	LOG_INF(
		"LED active: %s[%d:%s] -> %s[%d:%s] by %s[%d:%s] from %s:%d %s()",
		led_pattern_name(previous_pattern),
		previous_priority,
		led_priority_name(previous_priority),
		led_pattern_name(current_led_pattern),
		current_priority,
		led_priority_name(current_priority),
		led_pattern_name(requested_pattern),
		requested_priority,
		led_priority_name(requested_priority),
		source_name,
		source_line,
		func_name
	);
	if (current_led_pattern <= SYS_LED_PATTERN_OFF) {
		led_suspend();
		k_thread_suspend(led_thread_id);
		LOG_DBG("set_led: suspended led_thread_id");
	} else if (k_current_get() != led_thread_id) // do not suspend if called from thread
	{
		k_thread_suspend(led_thread_id);
		LOG_DBG("set_led: suspended led_thread_id");
		led_resume();
		k_thread_resume(led_thread_id);
		k_wakeup(led_thread_id);
		LOG_DBG("set_led: resumed led_thread_id");
	} else {
		led_resume();
		k_thread_resume(led_thread_id);
		k_wakeup(led_thread_id);
		LOG_DBG("set_led: resumed led_thread_id");
	}
#endif
}

static void led_thread(void)
{
#if !LED_EXISTS && !LED_STRIP_EXISTS
	LOG_WRN("LED GPIO does not exist");
	return;
#else
	while (1) {
		LOG_DBG("led_thread: current_led_pattern %d", current_led_pattern);
		switch (current_led_pattern) {
		case SYS_LED_PATTERN_ON:
			led_pin_set(SYS_LED_COLOR_DEFAULT, 10000, 10000);
			k_thread_suspend(led_thread_id);
			break;
		case SYS_LED_PATTERN_SHORT:
			led_pattern_state = (led_pattern_state + 1) % 2;
			led_pin_set(SYS_LED_COLOR_PAIRING, 10000, led_pattern_state * 10000);
			k_msleep(led_pattern_state == 1 ? 100 : 900);
			break;
		case SYS_LED_PATTERN_LONG:
			led_pattern_state = (led_pattern_state + 1) % 2;
			led_pin_set(SYS_LED_COLOR_DEFAULT, 10000, led_pattern_state * 10000);
			k_msleep(500);
			break;
		case SYS_LED_PATTERN_FLASH:
			led_pattern_state = (led_pattern_state + 1) % 2;
			led_pin_set(SYS_LED_COLOR_DEFAULT, 10000, led_pattern_state * 10000);
			k_msleep(200);
			break;

		case SYS_LED_PATTERN_ONESHOT_POWERON:
			led_pattern_state++;
			led_pin_set(SYS_LED_COLOR_DEFAULT, 10000, !(led_pattern_state % 2) * 10000);
			if (led_pattern_state == 7) {
				set_led(SYS_LED_PATTERN_OFF, SYS_LED_PRIORITY_HIGHEST);
			} else {
				k_msleep(200);
			}
			break;
		case SYS_LED_PATTERN_ONESHOT_POWEROFF:
			if (led_pattern_state++ > 0) {
				led_pin_set(
					SYS_LED_COLOR_DEFAULT,
					(202 - led_pattern_state) * 50,
					(led_pattern_state != 202 ? 10000 : 0)
				);
			} else {
				led_pin_set(SYS_LED_COLOR_DEFAULT, 10000, 0);
			}
			if (led_pattern_state == 202) {
				set_led(SYS_LED_PATTERN_OFF_FORCE, SYS_LED_PRIORITY_HIGHEST);
			} else if (led_pattern_state == 1) {
				k_msleep(250);
			} else {
				k_msleep(5);
			}
			break;
		case SYS_LED_PATTERN_ONESHOT_PROGRESS:
			led_pattern_state++;
			led_pin_set(SYS_LED_COLOR_SUCCESS, 10000, !(led_pattern_state % 2) * 10000);
			if (led_pattern_state == 5) {
				set_led(SYS_LED_PATTERN_OFF, SYS_LED_PRIORITY_HIGHEST);
			} else {
				k_msleep(200);
			}
			break;
		case SYS_LED_PATTERN_ONESHOT_COMPLETE:
			led_pattern_state++;
			led_pin_set(SYS_LED_COLOR_SUCCESS, 10000, !(led_pattern_state % 2) * 10000);
			if (led_pattern_state == 9) {
				set_led(SYS_LED_PATTERN_OFF, SYS_LED_PRIORITY_HIGHEST);
			} else {
				k_msleep(200);
			}
			break;
		case SYS_LED_PATTERN_ONESHOT_PING:
			led_pattern_state++;
			led_pin_set(SYS_LED_COLOR_DEFAULT, 10000, (led_pattern_state % 2) * 10000);
			if (led_pattern_state == 20) { // 10 flashes (states 1-20), turn off at 20
				set_led(SYS_LED_PATTERN_OFF, SYS_LED_PRIORITY_HIGHEST);
			} else {
				k_msleep(200);
			}
			break;

		case SYS_LED_PATTERN_ON_PERSIST:
			led_pin_set(SYS_LED_COLOR_SUCCESS, 2000, 10000);
			k_thread_suspend(led_thread_id);
			break;
		case SYS_LED_PATTERN_LONG_PERSIST:
			led_pattern_state = (led_pattern_state + 1) % 2;
			led_pin_set(SYS_LED_COLOR_CHARGING, 2000, led_pattern_state * 10000);
			k_msleep(500);
			break;
		case SYS_LED_PATTERN_PULSE_PERSIST: {
			led_pattern_state = (led_pattern_state + 1) % LED_PULSE_STEPS;
			int pulse_phase = led_pattern_state > LED_PULSE_HALF_STEPS
				? LED_PULSE_STEPS - led_pattern_state
				: led_pattern_state;
			int led_value = pulse_phase * LED_PULSE_CURVE_HALF_STEPS / LED_PULSE_HALF_STEPS;
			if (led_value < 200) {
				led_value = (led_value) * 30;
			} else if (led_value < 300) {
				led_value = (led_value - 200) * 20 + 6000;
			} else if (led_value < 400) {
				led_value = (led_value - 300) * 15 + 8000;
			} else {
				led_value = (led_value - 400) * 5 + 9500;
			}
			led_pin_set(SYS_LED_COLOR_CHARGING, 10000, led_value);
			k_msleep(CONFIG_LED_PULSE_UPDATE_MS);
			break;
		}
		case SYS_LED_PATTERN_ACTIVE_PERSIST: // off duration first because the device may turn on multiple times rapidly
											 // and waste battery power
			led_pattern_state = (led_pattern_state + 1) % 2;
			led_pin_set(SYS_LED_COLOR_DEFAULT, 10000, !led_pattern_state * 10000);
			k_msleep(led_pattern_state ? 9700 : 300);
			break;

		case SYS_LED_PATTERN_ERROR_A: // TODO: should this use 20% duty cycle?
			led_pattern_state = (led_pattern_state + 1) % 10;
			led_pin_set(SYS_LED_COLOR_ERROR, 10000, (led_pattern_state < 4 && led_pattern_state % 2) * 10000);
			k_msleep(500);
			break;
		case SYS_LED_PATTERN_ERROR_B:
			led_pattern_state = (led_pattern_state + 1) % 10;
			led_pin_set(SYS_LED_COLOR_ERROR, 10000, (led_pattern_state < 6 && led_pattern_state % 2) * 10000);
			k_msleep(500);
			break;
		case SYS_LED_PATTERN_ERROR_C:
			led_pattern_state = (led_pattern_state + 1) % 10;
			led_pin_set(SYS_LED_COLOR_ERROR, 10000, (led_pattern_state < 8 && led_pattern_state % 2) * 10000);
			k_msleep(500);
			break;
		case SYS_LED_PATTERN_ERROR_D:
			led_pattern_state = (led_pattern_state + 1) % 2;
			led_pin_set(SYS_LED_COLOR_ERROR, 10000, led_pattern_state * 10000);
			k_msleep(500);
			break;

		default:
			LOG_DBG("led_thread: suspending led_thread_id");
			k_thread_suspend(led_thread_id);
		}
	}
#endif
}
