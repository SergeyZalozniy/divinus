#include "gpio.h"

/* Two backends: the GPIO character device (kernels 4.8+), which only
 * addresses the lines of gpiochip0, and the legacy sysfs interface, which
 * takes absolute GPIO numbers. Whenever the character device is missing,
 * refuses an ioctl (e.g. ENOTTY on some vendor 4.9 kernels) or the pin lies
 * beyond the lines of gpiochip0, the access falls back to sysfs. */

#define GPIO_SYSFS_MAX 256

static char gpio_logged_cdev = 0, gpio_logged_sysfs = 0;

/* Pins already exported and set up by us, by direction */
static unsigned char gpio_sysfs_in[GPIO_SYSFS_MAX / 8];
static unsigned char gpio_sysfs_out[GPIO_SYSFS_MAX / 8];

#define GPIO_BIT_GET(map, pin) ((map)[(pin) >> 3] & (1 << ((pin) & 7)))
#define GPIO_BIT_SET(map, pin) ((map)[(pin) >> 3] |= (1 << ((pin) & 7)))
#define GPIO_BIT_CLR(map, pin) ((map)[(pin) >> 3] &= ~(1 << ((pin) & 7)))

static void gpio_backend_log(char sysfs) {
    if (sysfs && !gpio_logged_sysfs) {
        gpio_logged_sysfs = 1;
        HAL_INFO("gpio", "Using the sysfs interface (/sys/class/gpio)\n");
    } else if (!sysfs && !gpio_logged_cdev) {
        gpio_logged_cdev = 1;
        HAL_INFO("gpio", "Using the character device interface\n");
    }
}

/* The exported line's sysfs directory is "gpio<n>" on most kernels but
 * "GPIO<n>" on Fullhan kernels; probe both and cache the format. */
static const char *gpio_node(int pin) {
    static char fmt[24];
    static char dir[40];
    if (!fmt[0]) {
        char probe[40];
        sprintf(probe, "/sys/class/gpio/GPIO%d", pin);
        if (!access(probe, F_OK))
            strcpy(fmt, "/sys/class/gpio/GPIO%d");
        else {
            sprintf(probe, "/sys/class/gpio/gpio%d", pin);
            if (!access(probe, F_OK))
                strcpy(fmt, "/sys/class/gpio/gpio%d");
        }
    }
    sprintf(dir, fmt[0] ? fmt : "/sys/class/gpio/gpio%d", pin);
    return dir;
}

static int gpio_sysfs_export(int pin) {
    if (!access(gpio_node(pin), F_OK)) return EXIT_SUCCESS;

    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0)
        HAL_ERROR("gpio", "Unable to export GPIO pin %d (error #%d)!\n", pin, errno);

    char val[8];
    int len = sprintf(val, "%d", pin);
    /* EBUSY: the line is already exported (or claimed), not an error here */
    if (write(fd, val, len) < 0 && errno != EBUSY) {
        int err = errno;
        close(fd);
        HAL_ERROR("gpio", "Unable to export GPIO pin %d (error #%d)!\n", pin, err);
    }
    close(fd);

    /* udev-less systems create the node synchronously, give others a moment */
    for (int i = 0; i < 10 && access(gpio_node(pin), F_OK); i++)
        usleep(10000);

    return EXIT_SUCCESS;
}

static int gpio_sysfs_direction(int pin, const char *mode) {
    char path[56];
    snprintf(path, sizeof(path), "%s/direction", gpio_node(pin));
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        HAL_ERROR("gpio", "Unable to control the direction of GPIO pin %d!\n", pin);
    if (write(fd, mode, strlen(mode)) < 0) {
        close(fd);
        HAL_ERROR("gpio", "Unable to set the direction of GPIO pin %d!\n", pin);
    }
    close(fd);
    return EXIT_SUCCESS;
}

static int gpio_sysfs_read(int pin, bool *value) {
    if (pin < 0 || pin >= GPIO_SYSFS_MAX)
        HAL_ERROR("gpio", "GPIO pin %d is out of range!\n", pin);

    if (!GPIO_BIT_GET(gpio_sysfs_in, pin)) {
        if (gpio_sysfs_export(pin) || gpio_sysfs_direction(pin, "in"))
            return EXIT_FAILURE;
        GPIO_BIT_SET(gpio_sysfs_in, pin);
        GPIO_BIT_CLR(gpio_sysfs_out, pin);
    }

    char path[56];
    snprintf(path, sizeof(path), "%s/value", gpio_node(pin));
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        GPIO_BIT_CLR(gpio_sysfs_in, pin);
        HAL_ERROR("gpio", "Unable to read from GPIO pin %d!\n", pin);
    }

    char val = 0;
    if (read(fd, &val, 1) != 1 || (val != '0' && val != '1')) {
        close(fd);
        GPIO_BIT_CLR(gpio_sysfs_in, pin);
        HAL_ERROR("gpio", "Unable to read from GPIO pin %d!\n", pin);
    }
    close(fd);

    *value = val == '1';
    gpio_backend_log(1);
    return EXIT_SUCCESS;
}

static int gpio_sysfs_write(int pin, bool value) {
    if (pin < 0 || pin >= GPIO_SYSFS_MAX)
        HAL_ERROR("gpio", "GPIO pin %d is out of range!\n", pin);

    if (!GPIO_BIT_GET(gpio_sysfs_out, pin)) {
        /* "low"/"high" switch to output with the level set atomically */
        if (gpio_sysfs_export(pin) ||
            gpio_sysfs_direction(pin, value ? "high" : "low"))
            return EXIT_FAILURE;
        GPIO_BIT_SET(gpio_sysfs_out, pin);
        GPIO_BIT_CLR(gpio_sysfs_in, pin);
        gpio_backend_log(1);
        return EXIT_SUCCESS;
    }

    char path[56];
    snprintf(path, sizeof(path), "%s/value", gpio_node(pin));
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        GPIO_BIT_CLR(gpio_sysfs_out, pin);
        HAL_ERROR("gpio", "Unable to write to GPIO pin %d!\n", pin);
    }

    char val = value ? '1' : '0';
    if (write(fd, &val, 1) < 0) {
        close(fd);
        GPIO_BIT_CLR(gpio_sysfs_out, pin);
        HAL_ERROR("gpio", "Unable to write to GPIO pin %d!\n", pin);
    }
    close(fd);

    gpio_backend_log(1);
    return EXIT_SUCCESS;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
int fd_gpio = -1;

unsigned int gpio_count = 0;

/* Pins the character device failed on, served by sysfs from then on */
static unsigned char gpio_sysfs_only[GPIO_SYSFS_MAX / 8];

void gpio_deinit(void) {
    if (fd_gpio < 0) return;
    close(fd_gpio);
    fd_gpio = -1;
    gpio_count = 0;
}

int gpio_init(void) {
    if (fd_gpio >= 0) return EXIT_SUCCESS;

    if (access("/dev/gpiochip0", F_OK) ||
        (fd_gpio = open("/dev/gpiochip0", O_RDWR)) < 0) {
        fd_gpio = -1;
        HAL_INFO("gpio", "No usable GPIO character device, falling back to sysfs\n");
        return EXIT_SUCCESS;
    }

    struct gpiochip_info info;
    if (ioctl(fd_gpio, GPIO_GET_CHIPINFO_IOCTL, &info) == -1) {
        HAL_INFO("gpio", "Unable to enumerate the GPIO lines (error #%d), "
            "falling back to sysfs\n", errno);
        gpio_deinit();
        return EXIT_SUCCESS;
    }
    gpio_count = info.lines;

    return EXIT_SUCCESS;
}

static int gpio_cdev_read(int pin, bool *value) {
    struct gpiohandle_request req = { .lineoffsets[0] = pin, .lines = 1,
        .flags = GPIOHANDLE_REQUEST_INPUT };

    if (ioctl(fd_gpio, GPIO_GET_LINEHANDLE_IOCTL, &req) == -1)
        HAL_ERROR("gpio", "Unable to request a read on GPIO pin %d (error #%d)!\n", pin, errno);

    struct gpiohandle_data data;
    if (ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) == -1) {
        int err = errno;
        close(req.fd);
        HAL_ERROR("gpio", "Unable to read the value of GPIO pin %d (error #%d)!\n", pin, err);
    }

    *value = data.values[0];
    close(req.fd);
    gpio_backend_log(0);
    return EXIT_SUCCESS;
}

static int gpio_cdev_write(int pin, bool value) {
    struct gpiohandle_request req = { .default_values[0] = value, .lineoffsets[0] = pin,
        .lines = 1, .flags = GPIOHANDLE_REQUEST_OUTPUT };

    if (ioctl(fd_gpio, GPIO_GET_LINEHANDLE_IOCTL, &req) == -1)
        HAL_ERROR("gpio", "Unable to request a write on GPIO pin %d (error #%d)!\n", pin, errno);

    close(req.fd);
    gpio_backend_log(0);
    return EXIT_SUCCESS;
}

static inline char gpio_cdev_usable(int line) {
    return fd_gpio >= 0 && (unsigned int)line < gpio_count && !GPIO_BIT_GET(gpio_sysfs_only, line);
}

int gpio_read(char pin, bool *value) {
    int line = (unsigned char)pin;
    if (gpio_cdev_usable(line)) {
        if (!gpio_cdev_read(line, value)) return EXIT_SUCCESS;
        GPIO_BIT_SET(gpio_sysfs_only, line);
    }
    return gpio_sysfs_read(line, value);
}

int gpio_write(char pin, bool value) {
    int line = (unsigned char)pin;
    if (gpio_cdev_usable(line)) {
        if (!gpio_cdev_write(line, value)) return EXIT_SUCCESS;
        GPIO_BIT_SET(gpio_sysfs_only, line);
    }
    return gpio_sysfs_write(line, value);
}
#else
void gpio_deinit(void) {}

int gpio_init(void) {
    return EXIT_SUCCESS;
}

int gpio_read(char pin, bool *value) {
    return gpio_sysfs_read((unsigned char)pin, value);
}

int gpio_write(char pin, bool value) {
    return gpio_sysfs_write((unsigned char)pin, value);
}
#endif
