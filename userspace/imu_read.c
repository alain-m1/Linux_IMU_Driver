#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>

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

    unsigned char who_am_i_reg = 0x75;
    unsigned char who_am_i_val;

    if (write(fd, &who_am_i_reg, 1) != 1) {
        printf("Failed to request WHO_AM_I register.\n");
        close(fd);
        return 1;
    }
    if (read(fd, &who_am_i_val, 1) != 1) {
        printf("Failed to read WHO_AM_I register.\n");
        close(fd);
        return 1;
    }
    printf("WHO_AM_I returned: 0x%X\n", who_am_i_val);

    unsigned char wake_cmd[2] = {0x6B, 0x00};
    if (write(fd, wake_cmd, 2) != 2) {
        printf("Failed to wake up the sensor.\n");
        close(fd);
        return 1;
    }

    printf("MPU-6050 woken up successfully.\n");

    unsigned char start_reg = 0x3B;
    unsigned char data[14];

    if (write(fd, &start_reg, 1) != 1) {
        printf("Failed to request sensor data registers.\n");
        close(fd);
        return 1;
    }
    if (read(fd, data, 14) != 14) {
        printf("Failed to read sensor data.\n");
        close(fd);
        return 1;
    }

    int16_t raw_accel_x = (data[0] << 8) | data[1];
    int16_t raw_accel_y = (data[2] << 8) | data[3];
    int16_t raw_accel_z = (data[4] << 8) | data[5];
    int16_t raw_gyro_x = (data[8] << 8) | data[9];
    int16_t raw_gyro_y = (data[10] << 8) | data[11];
    int16_t raw_gyro_z = (data[12] << 8) | data[13];

    float accel_x = raw_accel_x / 16384.0;
    float accel_y = raw_accel_y / 16384.0;
    float accel_z = raw_accel_z / 16384.0;
    float gyro_x = raw_gyro_x / 131.0;
    float gyro_y = raw_gyro_y / 131.0;
    float gyro_z = raw_gyro_z / 131.0;

    printf("Accel: X=%.2f g  Y=%.2f g  Z=%.2f g\n", accel_x, accel_y, accel_z);
    printf("Gyro:  X=%.2f dps  Y=%.2f dps  Z=%.2f dps\n", gyro_x, gyro_y, gyro_z);

    close(fd);
    return 0;
}
