#ifndef NEUTRA_PCI_H
#define NEUTRA_PCI_H

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t dev;
    uint8_t fn;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
} PciDevice;

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t value);
int pci_find(uint16_t vendor, uint16_t device, PciDevice* out);
int pci_find_class(uint8_t class_code, uint8_t subclass, PciDevice* out);

#endif