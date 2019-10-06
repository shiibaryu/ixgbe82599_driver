#include <stdio.h>
#include <linux/limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lib.h"

void remove_driver(const char* pci_addr) {
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "/sys/bus/pci/devices/%s/driver/unbind", pci_addr);
	int fd = open(path, O_WRONLY);
	if (fd == -1) {
		debug("no driver loaded");
		return;
	}
	if (write(fd, pci_addr, strlen(pci_addr)) != (ssize_t) strlen(pci_addr)) {
		debug("failed to unload driver for device %s", pci_addr);
	}
	close(fd);
}

void enable_dma(const char* pci_addr) {
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "/sys/bus/pci/devices/%s/config", pci_addr);
	int fd = open(path, O_RDWR);
	if(fd < 0){
		debug("failed to open /%s/config",pci_addr);
	}
	lseek(fd, 4, SEEK_SET);
	uint16_t dma = 0;
	read(fd, &dma, 2);
	dma |= 1 << 2;
	lseek(fd, 4, SEEK_SET);
	write(fd, &dma, 2);
	close(fd);
}

uint8_t* pci_map_resource(const char* pci_addr) {
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "/sys/bus/pci/devices/%s/resource0", pci_addr);
	debug("Mapping PCI resource at %s", path);
	remove_driver(pci_addr);
	enable_dma(pci_addr);
	int fd = open(path, O_RDWR);
	struct stat stat;
	fstat(fd, &stat);
	return (uint8_t*)mmap(NULL, stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

int pci_open_resource(const char* pci_addr, const char* resource, int flags) {
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "/sys/bus/pci/devices/%s/%s", pci_addr, resource);
	debug("Opening PCI resource at %s", path);
	int fd = open(path, flags);
	return fd;
}
