#include "siren_hw.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define GPIO_EXPORT     "/sys/class/gpio/export"
#define GPIO_DIRECTION  "/sys/class/gpio/gpio595/direction"
#define GPIO_VALUE      "/sys/class/gpio/gpio595/value"

#define PWM_EXPORT      "/sys/class/pwm/pwmchip4/export"
#define PWM_PERIOD      "/sys/class/pwm/pwmchip4/pwm1/period"
#define PWM_DUTY_CYCLE  "/sys/class/pwm/pwmchip4/pwm1/duty_cycle"
#define PWM_ENABLE      "/sys/class/pwm/pwmchip4/pwm1/enable"

#define SIREN_GPIO_ID   "595"
#define SIREN_PWM_ID    "1"

/*
 * Target frequency/duty configuration.
 * These values come from the currently validated Versa siren setup.
 */
#define SIREN_PWM_PERIOD_NS      "485436"
#define SIREN_PWM_DUTY_CYCLE_NS  "242718"

static int write_sysfs(const char *path, const char *value)
{
    int fd;
    ssize_t ret;

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    ret = write(fd, value, strlen(value));
    close(fd);

    if (ret < 0) {
        perror(path);
        return -1;
    }

    return 0;
}

int siren_hw_init(void)
{
    write_sysfs(GPIO_EXPORT, SIREN_GPIO_ID);
    usleep(100000);

    write_sysfs(GPIO_DIRECTION, "out");
    write_sysfs(GPIO_VALUE, "1");

    write_sysfs(PWM_EXPORT, SIREN_PWM_ID);
    usleep(100000);

    write_sysfs(PWM_PERIOD, SIREN_PWM_PERIOD_NS);
    write_sysfs(PWM_DUTY_CYCLE, SIREN_PWM_DUTY_CYCLE_NS);

    write_sysfs(PWM_ENABLE, "0");

    fprintf(stdout, "[emergency-siren-service] hardware initialized\n");
    fflush(stdout);

    return 0;
}

void siren_hw_deinit(void)
{
    siren_hw_off();
}

int siren_hw_on(void)
{
    return write_sysfs(PWM_ENABLE, "1");
}

int siren_hw_off(void)
{
    return write_sysfs(PWM_ENABLE, "0");
}