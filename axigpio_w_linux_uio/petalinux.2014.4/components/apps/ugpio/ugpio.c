// http://forums.xilinx.com/t5/Embedded-Linux/UIO-interrupt-with-PS-GPIO/td-p/603502
// http://forums.xilinx.com/xlnx/attachments/xlnx/ELINUX/12938/1/gpio_uio_app.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

// This program implements a UIO driver in user space using an AXI GPIO in
// the PL. It expects an interrupt to be connected from the PL. The only purpose
// of the program is to simulate an interrupt from a device in the PL and to
// show accessing OCM memory from the UIO driver.
//
// This code assumes the GPIO device in the device tree has it's compatible
// string to be "generic-uio" and "uio_pdrv_genirq.of_id=generic-uio" is used
// on the command line via bootargs when the UIOP driver (uio_pdrv_genirq)
// driver is statically built into the kernel (the default).
//
// OCM is also used with the UIO driver such that it can be read/written from
// user space. It is mapped as device memory by UIO.
//
// The device tree should remove the OCM driver from the kernel such that
// OCM is not used by any kernel functions such as power mgmt for DDR self
// refresh mode. The GPIO node now has 2 memory regions with the 1st being
// the normal registers while the 2nd is the OCM memory.
//
// This was tested on a 702 board using the push buttons to generate an
// interrupt (left or right).

// The following snippet is used for system-top.dts with Petalinux.

// /dts-v1/;
// /include/ "system-conf.dtsi"
// /{
// };
// /{
//     model = "702-axi-gpio-irq";
//     aliases {
//        serial0 = &uart1;
//        ethernet0 = &gem0;
//     };
//     chosen {
//        bootargs = "console=ttyPS0,115200 earlyprintk uio_pdrv_genirq.of_id=generic-uio";
//     };
//     memory {
//        device_type = "memory";
//        reg = <0x0 0x40000000>;
//     };
// };
// &ocmc {
//	 compatible = "";
// };
// &axi_gpio_0 {
//	 compatible = "generic-uio";
//	 reg = <0x41200000 0x10000 0xFFFC0000 0x20000>;
// };
//

/**
 * My(sukinull) version: in subsystems/linux/configs/device-tree/system-top.dts
 * HW: ZedBoard
 * SW: petalinux 2014.4 w/ kernel 2.17
 * PL: gpio@41200000, GPIO1 => LED8, GPIO2 => BTN5
 * /dts-v1/;
 * /include/ "system-conf.dtsi"
 * / {
 *     amba_pl {
 *         gpio@41200000 {
 *             compatible = "generic-uio";
 *         };
 *     };
 * }/
 */
#define GPIO_MAP_SIZE 		0x10000
#define GPIO_DATA_OFFSET	0x00
#define GPIO_TRI_OFFSET		0x04
#define GPIO_DATA2_OFFSET	0x08
#define GPIO_TRI2_OFFSET	0x0C
#define GPIO_GLOBAL_IRQ		0x11C
#define GPIO_IRQ_CONTROL	0x128
#define GPIO_IRQ_STATUS		0x120

inline void gpio_write(void *gpio_base, unsigned int offset, unsigned int value)
{
	*((volatile unsigned *)(gpio_base + offset)) = value;
}

inline unsigned int gpio_read(void *gpio_base, unsigned int offset)
{
	return *((volatile unsigned *)(gpio_base + offset));
}

void wait_for_interrupt(int fd, void *gpio_ptr/*, unsigned int *ocm_ptr, int ocm_size*/)
{
	int pending = 0;
	int reenable = 1;
	unsigned int reg;
	int sum = 0, i;

	// block on the file waiting for an interrupt */

	read(fd, (void *)&pending, sizeof(int));

	// the interrupt occurred for the 2nd GPIO channel so clear it

	reg = gpio_read(gpio_ptr, GPIO_IRQ_STATUS);
	if (reg)
		gpio_write(gpio_ptr, GPIO_IRQ_STATUS, 2);

	reg = gpio_read(gpio_ptr, GPIO_DATA2_OFFSET);
	printf("GPIO_DATA2_OFFSET: 0x%d\n", reg);
#if 0
	// read the OCM memory and sum it's contents
	for (i = 0; i < ocm_size / sizeof(int); i++) {
		sum += *(ocm_ptr + i);
	}
	printf("sum = %d\n", sum);
#endif

	// re-enable the interrupt in the interrupt controller thru the
	// the UIO subsystem now that it's been handled

	write(fd, (void *)&reenable, sizeof(int));
}

unsigned int get_memory_size(char *sysfs_path_file)
{
	FILE *size_fp;
	unsigned int size;

	// open the file that describes the memory range size that is based on the
	// reg property of the node in the device tree

	size_fp = fopen(sysfs_path_file, "r");

	if (!size_fp) {
		printf("unable to open the uio size file\n");
		exit(-1);
	}

	// get the size which is an ASCII string such as 0xXXXXXXXX and then be stop
	// using the file

	fscanf(size_fp, "0x%08X", &size);
	fclose(size_fp);

	return size;
}

int main(int argc, char *argv[])
{
	int fd;
	char *uiod = "/dev/uio0";
	void *gpio_ptr;
	int gpio_size;
	// unsigned int *ocm_ptr;
	// int ocm_size;
	int i;

	printf("GPIO UIO test.\n");

	// open the UIO device file to allow access to the device in user space

	fd = open(uiod, O_RDWR);
	if (fd < 1) {
		printf("Invalid UIO device file:%s.\n", uiod);
		return -1;
	}

	gpio_size = get_memory_size("/sys/class/uio/uio0/maps/map0/size");
	// mmap the GPIO device into user space

	gpio_ptr = mmap(NULL, gpio_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (gpio_ptr == MAP_FAILED) {
		printf("Mmap call failure.\n");
		return -1;
	}

#if 0
	ocm_size = get_memory_size("/sys/class/uio/uio0/maps/map1/size");

	// mmap the OCM memory into user space

	ocm_ptr = mmap(NULL, ocm_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, getpagesize());
	if (ocm_ptr == MAP_FAILED) {
		printf("Mmap call failure.\n");
		return -1;
	}
#endif
	// make the GPIO bits to be outputs to drive the LEDs and the inputs from the switches

	gpio_write(gpio_ptr, GPIO_TRI_OFFSET, 0);
	gpio_write(gpio_ptr, GPIO_TRI2_OFFSET, 0xF);

	// enable the interrupts from the GPIO

	gpio_write(gpio_ptr, GPIO_GLOBAL_IRQ, 0x80000000);
	gpio_write(gpio_ptr, GPIO_IRQ_CONTROL, 2);

#if 0
	// initialize OCM just to test the summing in the irq
	for (i = 0; i < ocm_size / sizeof(int); i++) {
		*(ocm_ptr + i) = 0;
	}
#endif

	// wait for interrupts from the GPIO

	while (1) {
		wait_for_interrupt(fd, gpio_ptr);
	}

	// unmap the GPIO device and OCM from user space

	munmap(gpio_ptr, gpio_size);
#if 0
	munmap(ocm_ptr, ocm_size);
#endif
	return 0;
}

