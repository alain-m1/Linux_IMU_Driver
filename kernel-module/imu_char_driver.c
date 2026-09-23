#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "mympu6050"

#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_ACCEL_YOUT_H 0x3D
#define MPU6050_ACCEL_ZOUT_H 0x3F
#define MPU6050_GYRO_XOUT_H  0x43
#define MPU6050_GYRO_YOUT_H  0x45
#define MPU6050_GYRO_ZOUT_H  0x47

#define ACCEL_LSB_PER_G     16384
#define GYRO_LSB_PER_DPS    131

static s16 accel_x, accel_y, accel_z;
static s16 gyro_x, gyro_y, gyro_z;

static dev_t              imu_dev_num;
static struct cdev        imu_cdev;
static struct class      *imu_class;
static struct device     *imu_device;
static struct i2c_client *imu_client;

static void imu_sample(struct i2c_client *client) {
    s32 high, low;

    high = i2c_smbus_read_byte_data(client, MPU6050_ACCEL_XOUT_H);
    low  = i2c_smbus_read_byte_data(client, MPU6050_ACCEL_XOUT_H + 1);
    accel_x = (s16)((high << 8) | low);

    high = i2c_smbus_read_byte_data(client, MPU6050_ACCEL_YOUT_H);
    low  = i2c_smbus_read_byte_data(client, MPU6050_ACCEL_YOUT_H + 1);
    accel_y = (s16)((high << 8) | low);

    high = i2c_smbus_read_byte_data(client, MPU6050_ACCEL_ZOUT_H);
    low  = i2c_smbus_read_byte_data(client, MPU6050_ACCEL_ZOUT_H + 1);
    accel_z = (s16)((high << 8) | low);

    high = i2c_smbus_read_byte_data(client, MPU6050_GYRO_XOUT_H);
    low  = i2c_smbus_read_byte_data(client, MPU6050_GYRO_XOUT_H + 1);
    gyro_x = (s16)((high << 8) | low);

    high = i2c_smbus_read_byte_data(client, MPU6050_GYRO_YOUT_H);
    low  = i2c_smbus_read_byte_data(client, MPU6050_GYRO_YOUT_H + 1);
    gyro_y = (s16)((high << 8) | low);

    high = i2c_smbus_read_byte_data(client, MPU6050_GYRO_ZOUT_H);
    low  = i2c_smbus_read_byte_data(client, MPU6050_GYRO_ZOUT_H + 1);
    gyro_z = (s16)((high << 8) | low);
}

static void fixed_point(s32 centi, int *whole, int *frac) {
    *whole = centi / 100;
    *frac = centi % 100;
    if (*frac < 0) *frac = -*frac;
}

static int imu_open(struct inode *inode, struct file *file) {
    return 0;
}

static int imu_release(struct inode *inode, struct file *file) {
    return 0;
}

static ssize_t imu_read(struct file *file, char __user *buf, size_t count, loff_t *offset) {
    char msg[160];
    int len;
    s32 ax_c, ay_c, az_c, gx_c, gy_c, gz_c;
    int ax_w, ax_f, ay_w, ay_f, az_w, az_f;
    int gx_w, gx_f, gy_w, gy_f, gz_w, gz_f;

    if (*offset > 0)
        return 0;

    imu_sample(imu_client);

    ax_c = ((s32)accel_x * 100) / ACCEL_LSB_PER_G;
    ay_c = ((s32)accel_y * 100) / ACCEL_LSB_PER_G;
    az_c = ((s32)accel_z * 100) / ACCEL_LSB_PER_G;
    gx_c = ((s32)gyro_x  * 100) / GYRO_LSB_PER_DPS;
    gy_c = ((s32)gyro_y  * 100) / GYRO_LSB_PER_DPS;
    gz_c = ((s32)gyro_z  * 100) / GYRO_LSB_PER_DPS;

    fixed_point(ax_c, &ax_w, &ax_f);
    fixed_point(ay_c, &ay_w, &ay_f);
    fixed_point(az_c, &az_w, &az_f);
    fixed_point(gx_c, &gx_w, &gx_f);
    fixed_point(gy_c, &gy_w, &gy_f);
    fixed_point(gz_c, &gz_w, &gz_f);

    len = snprintf(msg, sizeof(msg),
                   "accel(g): x=%d.%02d y=%d.%02d z=%d.%02d\n"
                   "gyro(dps): x=%d.%02d y=%d.%02d z=%d.%02d\n",
                   ax_w, ax_f, ay_w, ay_f, az_w, az_f,
                   gx_w, gx_f, gy_w, gy_f, gz_w, gz_f);

    if (len > count)
        len = count;

    if (copy_to_user(buf, msg, len))
        return -EFAULT;

    *offset += len;
    return len;
}

static const struct file_operations imu_fops = {
    .owner   = THIS_MODULE,
    .open    = imu_open,
    .read    = imu_read,
    .release = imu_release,
};

static int mympu6050_probe(struct i2c_client *client) {
    int ret;

    printk(KERN_INFO "mympu6050: probe() called - device found!\n");

    ret = i2c_smbus_write_byte_data(client, MPU6050_PWR_MGMT_1, 0x00);
    if (ret < 0) {
        printk(KERN_ERR "mympu6050: failed to wake device (%d)\n", ret);
        return ret;
    }

    imu_client = client;
    imu_sample(imu_client);

    printk(KERN_INFO "mympu6050: accel(x=%d y=%d z=%d) gyro(x=%d y=%d z=%d)\n",
           accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z);

    ret = alloc_chrdev_region(&imu_dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "mympu6050: failed to allocate chrdev region (%d)\n", ret);
        return ret;
    }

    cdev_init(&imu_cdev, &imu_fops);
    imu_cdev.owner = THIS_MODULE;

    ret = cdev_add(&imu_cdev, imu_dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "mympu6050: failed to add cdev (%d)\n", ret);
        unregister_chrdev_region(imu_dev_num, 1);
        return ret;
    }

    imu_class = class_create(DEVICE_NAME);
    if (IS_ERR(imu_class)) {
        printk(KERN_ERR "mympu6050: failed to create class\n");
        cdev_del(&imu_cdev);
        unregister_chrdev_region(imu_dev_num, 1);
        return PTR_ERR(imu_class);
    }

    imu_device = device_create(imu_class, NULL, imu_dev_num, NULL, "imu0");
    if (IS_ERR(imu_device)) {
        printk(KERN_ERR "mympu6050: failed to create device\n");
        class_destroy(imu_class);
        cdev_del(&imu_cdev);
        unregister_chrdev_region(imu_dev_num, 1);
        return PTR_ERR(imu_device);
    }

    printk(KERN_INFO "mympu6050: /dev/imu0 created\n");

    return 0;
}

static void mympu6050_remove(struct i2c_client *client) {
    device_destroy(imu_class, imu_dev_num);
    class_destroy(imu_class);
    cdev_del(&imu_cdev);
    unregister_chrdev_region(imu_dev_num, 1);
    printk(KERN_INFO "mympu6050: remove() called - device removed.\n");
}

static const struct i2c_device_id mympu6050_id[] = {
    { DEVICE_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mympu6050_id);

static struct i2c_driver mympu6050_driver = {
    .driver = {
        .name = DEVICE_NAME,
    },
    .probe = mympu6050_probe,
    .remove = mympu6050_remove,
    .id_table = mympu6050_id,
};

static int __init mympu6050_init(void) {
    printk(KERN_INFO "mympu6050: module loaded.\n");
    return i2c_add_driver(&mympu6050_driver);
}

static void __exit mympu6050_exit(void) {
    i2c_del_driver(&mympu6050_driver);
    printk(KERN_INFO "mympu6050: module unloaded.\n");
}

module_init(mympu6050_init);
module_exit(mympu6050_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alain Mignot");
MODULE_DESCRIPTION("Skeleton I2C driver for MPU-6050 character device project");
