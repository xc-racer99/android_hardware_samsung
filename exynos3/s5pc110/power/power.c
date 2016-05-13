/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LOG_TAG "S5PC110 PowerHAL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

#define CPUFREQ_INTERACTIVE "/sys/devices/system/cpu/cpufreq/interactive/"
#define CPUFREQ_CPU0 "/sys/devices/system/cpu/cpu0/cpufreq/"
#define BOOSTPULSE_PATH (CPUFREQ_INTERACTIVE "boostpulse")
#define SCALINGMAXFREQ_PATH (CPUFREQ_CPU0 "scaling_max_freq")

#define MAX_BUF_SZ  10

/* initialize to something safe */
static char screen_off_max_freq[MAX_BUF_SZ] = "800000";
static char scaling_max_freq[MAX_BUF_SZ] = "1000000";

struct s5pc110_power_module {
    struct power_module base;
    pthread_mutex_t lock;
    int boostpulse_fd;
    int boostpulse_warned;
    int inited;
};

static void sysfs_write(char *path, char *s)
{
    char buf[80];
    int len;
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error opening %s: %s\n", path, buf);
        return;
    }

    len = write(fd, s, strlen(s));
    if (len < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error writing to %s: %s\n", path, buf);
    }

    close(fd);
}

int sysfs_read(const char *path, char *buf, size_t size)
{
  int fd, len;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  do {
    len = read(fd, buf, size);
  } while (len < 0 && errno == EINTR);

  close(fd);

  return len;
}

static void s5pc110_power_init(struct power_module *module)
{
    struct s5pc110_power_module *s5pc110 = (struct s5pc110_power_module *) module;

    sysfs_write(CPUFREQ_INTERACTIVE "timer_rate", "20000");
    sysfs_write(CPUFREQ_INTERACTIVE "min_sample_time", "60000");
    sysfs_write(CPUFREQ_INTERACTIVE "hispeed_freq", "1000000");
    sysfs_write(CPUFREQ_INTERACTIVE "target_loads", "70 800000:80 1000000:90");
    sysfs_write(CPUFREQ_INTERACTIVE "go_hispeed_load", "99");
    sysfs_write(CPUFREQ_INTERACTIVE "above_hispeed_delay", "80000");

    ALOGI("Initialized successfully");
    s5pc110->inited = 1;
}

static int boostpulse_open(struct s5pc110_power_module *s5pc110)
{
    char buf[80];

    pthread_mutex_lock(&s5pc110->lock);

    if (s5pc110->boostpulse_fd < 0) {
        s5pc110->boostpulse_fd = open(BOOSTPULSE_PATH, O_WRONLY);

        if (s5pc110->boostpulse_fd < 0) {
            if (!s5pc110->boostpulse_warned) {
                strerror_r(errno, buf, sizeof(buf));
                ALOGE("Error opening %s: %s\n", BOOSTPULSE_PATH, buf);
                s5pc110->boostpulse_warned = 1;
            }
        }
    }

    pthread_mutex_unlock(&s5pc110->lock);
    return s5pc110->boostpulse_fd;
}

static void s5pc110_power_set_interactive(struct power_module *module, int on)
{
    struct s5pc110_power_module *s5pc110 = (struct s5pc110_power_module *) module;
    int len;
    char buf[MAX_BUF_SZ];

    if (!s5pc110->inited) {
        return;
    }

    /*
     * Lower maximum frequency when screen is off.  CPU 0 and 1 share a
     * cpufreq policy.
     */
    if (!on) {
        /* read the current scaling max freq and save it before updating */
        len = sysfs_read(SCALINGMAXFREQ_PATH, buf, sizeof(buf));

        /* make sure it's not the screen off freq, if the "on"
         * call is skipped (can happen if you press the power
         * button repeatedly) we might have read it. We should
         * skip it if that's the case
         */
        if (len != -1 && strncmp(buf, screen_off_max_freq,
                strlen(screen_off_max_freq)) != 0)
            memcpy(scaling_max_freq, buf, sizeof(buf));
        sysfs_write(SCALINGMAXFREQ_PATH, screen_off_max_freq);
    } else
        sysfs_write(SCALINGMAXFREQ_PATH, scaling_max_freq);
}

static void s5pc110_power_hint(struct power_module *module, power_hint_t hint,
                            void *data __unused)
{
    struct s5pc110_power_module *s5pc110 = (struct s5pc110_power_module *) module;
    char buf[80];
    int len;

    if (!s5pc110->inited) {
        return;
    }

    switch (hint) {
    case POWER_HINT_INTERACTION:
        if (boostpulse_open(s5pc110) >= 0) {
	    len = write(s5pc110->boostpulse_fd, "1", 1);

	    if (len < 0) {
	        strerror_r(errno, buf, sizeof(buf));
		ALOGE("Error writing to %s: %s\n", BOOSTPULSE_PATH, buf);
	    }
	}
        break;

    case POWER_HINT_VSYNC:
        break;

    default:
        break;
    }
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct s5pc110_power_module HAL_MODULE_INFO_SYM = {
    .base = {
        .common = {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = POWER_MODULE_API_VERSION_0_2,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = POWER_HARDWARE_MODULE_ID,
            .name = "S5PC110 Power HAL",
            .author = "The Android Open Source Project",
            .methods = &power_module_methods,
        },

        .init = s5pc110_power_init,
        .setInteractive = s5pc110_power_set_interactive,
        .powerHint = s5pc110_power_hint,
    },

    .lock = PTHREAD_MUTEX_INITIALIZER,
    .boostpulse_fd = -1,
    .boostpulse_warned = 0,
};

