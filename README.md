# Linux IMU Driver

Reads live accelerometer and gyroscope data from an MPU-6050 (GY-521 breakout) on a Raspberry Pi, two different ways: as an ordinary userspace C program talking to the I2C bus through the kernel's generic interface (Phase 1), and as a real Linux kernel character-device driver that owns the I2C communication itself (Phase 2). Both phases perform the same register-level reads and unit conversions. No vendor driver library is used in either case.

This project follows on from an earlier bare-metal Arduino implementation of the same sensor (interrupt-driven, register-level I2C on an ATmega328P). The goal here is to build the same "read the sensor" logic up through increasing levels of the Linux I/O stack: raw syscalls in userspace, then a proper kernel driver.

## Hardware

- Raspberry Pi (any model with a 40-pin GPIO header and I2C support)
- MPU-6050 / GY-521 breakout (accelerometer + gyroscope, I2C)
- Jumper wires (breadboard optional)

## Wiring

| MPU-6050 | Raspberry Pi                                                                                           |
|----------|--------------------------------------------------------------------------------------------------------|
| VCC      | **3.3V** (physical pin 1) - The Pi's GPIO pins are only rated for 3.3V logic and can be damaged by 5V. |
| GND      | GND (e.g. physical pin 6)                                                                              |
| SDA      | GPIO2 (physical pin 3)                                                                                 |
| SCL      | GPIO3 (physical pin 5)                                                                                 |

## I2C Bus Address

| Device   | Address                          |
|----------|----------------------------------|
| MPU-6050 | `0x68` (default, `AD0` tied low) |

## Key MPU-6050 Registers Used

See the MPU-6000/6050 Register Map and Descriptions datasheet for the full reference.

| Register      | Name                          | Purpose                                                                              |
|---------------|-------------------------------|--------------------------------------------------------------------------------------|
| `0x6B`        | `PWR_MGMT_1`                  | Power management; writing `0x00` wakes the chip from its default sleep-on-boot state |
| `0x75`        | `WHO_AM_I`                    | Read-only identity register; expected to return `0x68`                               |
| `0x3B`-`0x40` | `ACCEL_XOUT_H`…`ACCEL_ZOUT_L` | Accelerometer X/Y/Z, big-endian 16-bit signed, high byte first                       |
| `0x43`-`0x48` | `GYRO_XOUT_H`…`GYRO_ZOUT_L`   | Gyroscope X/Y/Z, big-endian 16-bit signed, high byte first                           |

**Raw-to-physical-unit conversion** (default sensitivity settings):
- Accelerometer: ±2g full scale --> divide raw value by `16384.0` for g
- Gyroscope: ±250 deg/s full scale --> divide raw value by `131.0` for deg/sec

Phase 1 applies this conversion with ordinary floating-point division. Phase 2 applies the same ratios using integer fixed-point math instead, since kernel code can't safely use floats (see the Phase 2 section below for details).

---

## Phase 1 - Userspace Program

A plain C program, compiled and run like any other Linux program with no special privileges or kernel code. It opens the I2C bus device file directly, tells the kernel which device address to talk to, and reads/writes MPU-6050 registers using ordinary `write()`/`read()` syscalls.

### How it works

- `open("/dev/i2c-1", O_RDWR)` - gets a file handle to the I2C bus
- `ioctl(fd, I2C_SLAVE, 0x68)` - tells the kernel the target device address for this file descriptor
- `write()` of `0x6B, 0x00` - wakes the MPU-6050 from sleep (same as `PWR_MGMT_1` register write)
- `write()` of `0x75` then `read()` of 1 byte - WHO_AM_I identity check, expects `0x68`
- `write()` of `0x3B` then `read()` of 14 bytes - one burst read covering accel, temperature (discarded), and gyro
- Raw register values are converted to physical units using the sensitivity constants above

### Dependencies

- Raspberry Pi OS with a reachable terminal (local or SSH)
- I2C enabled at the OS level (disabled by default)
- `i2c-tools` (for bus verification, not required by the program itself)
- `gcc` / `build-essential`

### Setup

```bash
sudo raspi-config     # Interface Options --> I2C --> Enable, then reboot
sudo apt install i2c-tools build-essential
```

### Verify wiring before running any code

```bash
i2cdetect -y 1
```
You should see `68` appear in the printed grid, confirming the MPU-6050 is responding on the bus.

### Build and run

```bash
cd userspace
gcc imu_read.c -o imu_read
./imu_read
```

You may need `sudo` the first time, until your user is added to the `i2c` group:
```bash
sudo usermod -aG i2c $USER   # then log out/in for it to take effect
```

### Expected output

The program should print a `WHO_AM_I returned: 0x68` confirmation followed by live accelerometer/gyroscope readings. Tilt the board and confirm the values respond.

---

## Phase 2 - Kernel Module / Character Device Driver

A loadable kernel module that registers itself as an I2C driver for the MPU-6050 and exposes the sensor as `/dev/imu0`, readable with an ordinary `cat`. Unlike Phase 1, all I2C communication here happens inside the kernel itself, using the kernel's own I2C API rather than syscalls on a bus device file.

### How it works

- An `i2c_driver` struct with a device ID table tells the kernel "I know how to handle a device named `mympu6050`."
- `probe()` runs automatically once a matching device is registered on the bus. It wakes the chip with `i2c_smbus_write_byte_data()`, takes an initial sample, and registers a character device.
- `alloc_chrdev_region()`, `cdev_init()`/`cdev_add()`, and `class_create()`/`device_create()` together create `/dev/imu0` automatically.
- The driver's `file_operations` table implements `.open`, `.read`, and `.release`. Every `.read` call re-samples the sensor with another `i2c_smbus_read_byte_data()` burst across each accel/gyro axis (high byte + low byte) before formatting the values as plain text and handing them to the calling process with `copy_to_user()`. This means every `cat /dev/imu0` reflects the sensor's current orientation, not a cached value.
- Raw register counts are converted to g and deg/sec using fixed-point integer math rather than floating point. The kernel doesn't save/restore FPU state across context switches the way userspace does, so ordinary float division isn't safe inside a driver. Instead, each raw value is scaled by 100 (`(raw * 100) / 16384` for accel, `(raw * 100) / 131` for gyro) to preserve two decimal places as an integer "centi-g"/"centi-deg/sec" value, then split into whole and fractional parts for display.
- `remove()` reverses everything from `probe()` like `device_destroy()`, `class_destroy()`, `cdev_del()`, and `unregister_chrdev_region()` so that unloading the module doesn't leave a stale device node behind.

### Dependencies

- Everything from Phase 1 (I2C enabled, wiring verified)
- Kernel headers matching your running kernel:
  ```bash
  sudo apt install raspberrypi-kernel-headers
  ```
  (or `linux-headers-$(uname -r)` on non-Raspberry-Pi-OS setups)
- `make` and `gcc`

### Build

```bash
cd kernel-module
make clean
make
```
This produces `imu_char_driver.ko`.

### Load and test

```bash
# Load the module (no device yet - nothing happens until one is registered)
sudo insmod imu_char_driver.ko

# Register the device at the MPU-6050's bus address - this triggers probe()
echo mympu6050 0x68 | sudo tee /sys/bus/i2c/devices/i2c-1/new_device

# Confirm probe() ran, the sensor woke up, and /dev/imu0 was created
dmesg | tail

# Read the sensor data (root-owned device node by default)
sudo cat /dev/imu0
```

Expected `cat` output looks something like:
```
accel(g): x=0.42 y=0.00 z=0.89
gyro(dps): x=-0.91 y=-0.10 z=1.32
```

Since the sensor is re-sampled on every read, running `cat /dev/imu0` again after tilting the board will reflect its new orientation.

### Clean up (always do this before reloading or unloading)

```bash
echo 0x68 | sudo tee /sys/bus/i2c/devices/i2c-1/delete_device
sudo rmmod imu_char_driver
```
Skipping `delete_device` before `rmmod` leaves the I2C client registered in the kernel even after the driver unloads. That means reloading the module would then auto-trigger `probe()` again on the leftover client instead of waiting for a fresh `new_device` command, and a duplicate `new_device` attempt would fail with `EBUSY`.

### Verifying a clean unload

```bash
ls -l /dev/imu0                     # should output "No such file or directory"
lsmod | grep imu_char_driver        # should print nothing
```

