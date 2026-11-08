#include "night.h"

char nightOn = 0;
static bool grayscale = false, ircut = true, irled = false, manual = false;
pthread_t nightPid = 0;

bool night_grayscale_on(void) { return grayscale; }

bool night_ircut_on(void) { return ircut; }

bool night_irled_on(void) { return irled; }

bool night_manual_on(void) { return manual; }

bool night_mode_on(void) { return grayscale && !ircut && irled; }

void night_grayscale(bool enable) {
    set_grayscale(enable);
    grayscale = enable;
}

void night_ircut(bool enable) {
    bool has1 = app_config.ir_cut_pin1 != 999;
    bool has2 = app_config.ir_cut_pin2 != 999;

    if (has1) gpio_write(app_config.ir_cut_pin1, !enable);
    if (has2) gpio_write(app_config.ir_cut_pin2, enable);
    if (has1 || has2) {
        usleep(app_config.pin_switch_delay_us * 100);
        if (has1) gpio_write(app_config.ir_cut_pin1, false);
        if (has2) gpio_write(app_config.ir_cut_pin2, false);
    }
    ircut = enable;
}

void night_irled(bool enable) {
    if (app_config.ir_led_pin != 999)
        gpio_write(app_config.ir_led_pin, enable);
    irled = enable;
}

static void night_lamp(bool enable) {
#if defined(__arm__) && !defined(__ARM_PCS_VFP) && __ARM_ARCH == 6
    if (plat == HAL_PLATFORM_F1)
        fh_lamp_enable(enable ? app_config.night_lamp : HAL_LIGHT_NONE);
#endif
}

void night_manual(bool enable) { manual = enable; }

static bool sensed = false;
static unsigned int pending = 0;

/* Feeds one sensor reading (true = dark) taken step_s seconds after the
 * previous one. The first reading after the thread starts applies at once;
 * afterwards the mode only flips once the readings have disagreed with the
 * active mode for night_delay_s (going dark) or day_delay_s (going bright)
 * seconds in a row. With both delays at 0 it flips on the first differing
 * reading, and an unchanged reading no longer re-pulses the IR-cut coil. */
static void night_sense(bool dark, unsigned int step_s) {
    if (manual || dark == night_mode_on()) {
        sensed = true;
        pending = 0;
        return;
    }

    if (!sensed) {
        sensed = true;
        pending = 0;
        night_mode(dark);
        return;
    }

    unsigned int delay = dark ? app_config.night_delay_s : app_config.day_delay_s;
    if (pending >= delay) {
        pending = 0;
        night_mode(dark);
    } else {
        HAL_INFO("night", "Sensor reads %s, switching in %us unless it changes back\n",
            dark ? "dark" : "bright", delay - pending);
        pending += step_s ? step_s : 1;
    }
}

void night_mode(bool enable) {
    HAL_INFO("night", "Changing mode to %s\n", enable ? "NIGHT" : "DAY");
#if defined(__arm__) && !defined(__ARM_PCS_VFP) && __ARM_ARCH == 6
    if (plat == HAL_PLATFORM_F1) {
        hal_light lamp = app_config.night_lamp;
        if (!enable) {
            night_grayscale(false);
            night_ircut(true);
            night_irled(false);
            night_lamp(false);
        } else if (lamp & HAL_LIGHT_WHITE) {
            night_grayscale(false);
            night_ircut(true);
            night_irled((lamp & HAL_LIGHT_IR) != 0);
            night_lamp(true);
        } else {
            night_grayscale(true);
            night_ircut(false);
            night_irled((lamp & HAL_LIGHT_IR) != 0);
            night_lamp(true);
        }
        return;
    }
#endif
    night_grayscale(enable);
    night_ircut(!enable);
    night_irled(enable);
}

void *night_thread(void) {
    gpio_init();
    usleep(10000);

    sensed = false;
    pending = 0;

    if (!manual) night_mode(night_mode_on());
    else night_lamp(night_mode_on());

#if defined(__arm__) && !defined(__ARM_PCS_VFP) && __ARM_ARCH == 6
    if (plat == HAL_PLATFORM_F1 && f1_night_available() &&
        !app_config.adc_device[0] && app_config.ir_sensor_pin == 999) {
        HAL_INFO("night", "Using image gain for day/night switching\n");
        int applied = -1;
        while (keepRunning && nightOn) {
            int state = f1_night_status();
            if (manual) applied = -1;
            else if (state != applied) { night_mode(state); applied = state; }
            usleep(40000);
        }
    } else
#endif
    if (app_config.adc_device[0]) {
        int adc_fd = -1;
        fd_set adc_fds;
        int cnt = 0, tmp = 0, val;

        if ((adc_fd = open(app_config.adc_device, O_RDONLY | O_NONBLOCK)) <= 0) {
            HAL_DANGER("night", "Could not open the ADC virtual device!\n");
            return NULL;
        }
        while (keepRunning && nightOn) {
            if (read(adc_fd, &val, sizeof(val)) > 0) {
                usleep(10000);
                tmp += val;
                cnt++;
            }
            if (cnt == 12) {
                tmp /= cnt;
                bool dark = tmp >= app_config.adc_threshold;
                night_sense(app_config.ir_sensor_invert ? !dark : dark,
                    app_config.check_interval_s);
                cnt = tmp = 0;
            }
            usleep(app_config.check_interval_s * 1000000 / 12);
        }
        if (adc_fd) close(adc_fd);
    } else if (app_config.ir_sensor_pin == 999) {
        while (keepRunning && nightOn) sleep(1);
    } else {
        while (keepRunning && nightOn) {
            bool state = false;
            if (!gpio_read(app_config.ir_sensor_pin, &state))
                night_sense(app_config.ir_sensor_invert ? !state : state,
                    app_config.check_interval_s);

            /* Sleep in short steps so a shutdown or a reconfiguration
               does not wait for a whole check interval */
            for (unsigned int i = 0; i < app_config.check_interval_s &&
                keepRunning && nightOn; i++)
                sleep(1);
        }
    }

    usleep(10000);
    gpio_deinit();
    HAL_INFO("night", "Night mode thread is closing...\n");
    nightOn = 0;
}

int night_enable(void) {
    int ret = EXIT_SUCCESS;

    if (nightOn) return ret;

    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    size_t stacksize;
    pthread_attr_getstacksize(&thread_attr, &stacksize);
    size_t new_stacksize = 16 * 1024;
    if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
        HAL_DANGER("night", "Error:  Can't set stack size %zu\n", new_stacksize);
    pthread_create(&nightPid, &thread_attr, (void *(*)(void *))night_thread, NULL);
    if (pthread_attr_setstacksize(&thread_attr, stacksize))
        HAL_DANGER("night", "Error:  Can't set stack size %zu\n", stacksize);
    pthread_attr_destroy(&thread_attr);

    nightOn = 1;

    return ret;
}

void night_disable(void) {
    if (!nightOn) return;

    nightOn = 0;
    pthread_join(nightPid, NULL);
}
