#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>

#define DEVICE_NAME "mympu6050"

static int mympu6050_probe(struct i2c_client *client) {
    printk(KERN_INFO "mympu6050: probe() called - device found!\n");
    return 0;
}

static void mympu6050_remove(struct i2c_client *client) {
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
