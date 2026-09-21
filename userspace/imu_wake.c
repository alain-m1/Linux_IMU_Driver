#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define I2C_BUS "/dev/i2c-1"
#define MPU_ADDR 0x68

int main() {
    int fd = open(I2C_BUS, O_RDWR);
    if (fd < 0) {
        printf("Failed to open the I2C bus.\n");
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, MPU_ADDR) < 0) {
        printf("Failed to connect to the MPU-6050.\n");
        close(fd);
        return 1;
    }

    unsigned char wake_cmd[2] = {0x6B, 0x00};
    if (write(fd, wake_cmd, 2) != 2) {
        printf("Failed to wake up the sensor.\n");
        close(fd);
        return 1;
    }

    printf("MPU-6050 woken up successfully.\n");

    close(fd);
    return 0;
}
