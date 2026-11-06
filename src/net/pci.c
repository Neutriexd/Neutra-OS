#include "pci.h"
#include "net_io.h"

static uint32_t config_address(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 31) << 11) |
           ((uint32_t)(fn & 7) << 8) | (offset & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    net_outl(0xCF8, config_address(bus, dev, fn, offset));
    return net_inl(0xCFC);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t value) {
    net_outl(0xCF8, config_address(bus, dev, fn, offset));
    net_outl(0xCFC, value);
}

static int scan(int want_ids, uint16_t vendor, uint16_t device, uint8_t class_code, uint8_t subclass, PciDevice* out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            const uint32_t id = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            const uint32_t klass = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x08);
            const uint8_t cls = (uint8_t)(klass >> 24);
            const uint8_t sub = (uint8_t)(klass >> 16);
            const uint16_t ven = (uint16_t)(id & 0xFFFF);
            const uint16_t dvc = (uint16_t)(id >> 16);
            const int match = want_ids ? (ven == vendor && dvc == device) : (cls == class_code && sub == subclass);
            if (match) {
                out->bus = (uint8_t)bus;
                out->dev = (uint8_t)dev;
                out->fn = 0;
                out->vendor = ven;
                out->device = dvc;
                out->class_code = cls;
                out->subclass = sub;
                return 1;
            }
        }
    }
    return 0;
}

int pci_find(uint16_t vendor, uint16_t device, PciDevice* out) {
    return scan(1, vendor, device, 0, 0, out);
}

int pci_find_class(uint8_t class_code, uint8_t subclass, PciDevice* out) {
    return scan(0, 0, 0, class_code, subclass, out);
}