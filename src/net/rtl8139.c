#include "rtl8139.h"
#include "pci.h"
#include "net_io.h"

#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139

#define REG_MAC0 0x00
#define REG_TSD0 0x10
#define REG_TSAD0 0x20
#define REG_RBSTART 0x30
#define REG_CR 0x37
#define REG_CAPR 0x38
#define REG_CBR 0x3A
#define REG_IMR 0x3C
#define REG_ISR 0x3E
#define REG_TCR 0x40
#define REG_RCR 0x44
#define REG_CONFIG1 0x52

#define CR_RST 0x10
#define CR_RE 0x08
#define CR_TE 0x04
#define CR_BUFE 0x01
#define TSD_TOK 0x8000
#define RCR_ACCEPT_ALL 0x0F
#define RCR_WRAP 0x80

#define RX_RING 8192
#define RX_BUF_SIZE (RX_RING + 16 + 1536)
#define TX_SLOTS 4
#define TX_SLOT_SIZE 1536
#define MIN_FRAME 60
#define MAX_FRAME 1514

static uint16_t io_base;
static int ready;
static uint8_t mac[6];
static uint32_t rx_offset;
static int tx_slot;
static uint8_t rx_buf[RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buf[TX_SLOTS][TX_SLOT_SIZE] __attribute__((aligned(16)));

static void enable_bus_master(const PciDevice* card) {
    const uint32_t command = pci_read32(card->bus, card->dev, card->fn, 0x04);
    pci_write32(card->bus, card->dev, card->fn, 0x04, (command & 0xFFFF) | 0x0004);
}

int rtl8139_init(void) {
    PciDevice card;
    if (!pci_find(RTL_VENDOR, RTL_DEVICE, &card)) return -1;

    const uint32_t bar0 = pci_read32(card.bus, card.dev, card.fn, 0x10);
    if (!(bar0 & 1)) return -2;
    io_base = (uint16_t)(bar0 & ~3u);
    enable_bus_master(&card);

    net_outb(io_base + REG_CONFIG1, 0x00);
    net_outb(io_base + REG_CR, CR_RST);
    for (int i = 0; i < 100000 && (net_inb(io_base + REG_CR) & CR_RST); i++) {
    }
    if (net_inb(io_base + REG_CR) & CR_RST) return -3;

    for (int i = 0; i < 6; i++) mac[i] = net_inb(io_base + REG_MAC0 + i);

    for (int i = 0; i < RX_BUF_SIZE; i++) rx_buf[i] = 0;
    net_outl(io_base + REG_RBSTART, (uint32_t)(uintptr_t)rx_buf);
    net_outw(io_base + REG_IMR, 0x0000);
    net_outl(io_base + REG_RCR, RCR_ACCEPT_ALL | RCR_WRAP);
    net_outl(io_base + REG_TCR, 0x03000700);
    for (int i = 0; i < TX_SLOTS; i++) {
        net_outl(io_base + REG_TSAD0 + i * 4, (uint32_t)(uintptr_t)tx_buf[i]);
    }

    rx_offset = 0;
    tx_slot = 0;
    net_outw(io_base + REG_CAPR, (uint16_t)(0 - 16));
    net_outb(io_base + REG_CR, CR_RE | CR_TE);
    ready = 1;
    return 0;
}

int rtl8139_ready(void) {
    return ready;
}

void rtl8139_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = mac[i];
}

int rtl8139_send(const uint8_t* frame, int length) {
    if (!ready || frame == 0 || length <= 0 || length > MAX_FRAME) return -1;

    uint8_t* slot = tx_buf[tx_slot];
    for (int i = 0; i < length; i++) slot[i] = frame[i];
    int total = length;
    while (total < MIN_FRAME) slot[total++] = 0;

    const uint16_t tsd = (uint16_t)(REG_TSD0 + tx_slot * 4);
    net_outl(io_base + tsd, (uint32_t)total);
    for (int i = 0; i < 1000000; i++) {
        if (net_inl(io_base + tsd) & TSD_TOK) break;
    }
    const int done = (net_inl(io_base + tsd) & TSD_TOK) != 0;
    tx_slot = (tx_slot + 1) % TX_SLOTS;
    return done ? 0 : -4;
}

int rtl8139_receive(uint8_t* out, int max) {
    if (!ready || out == 0 || max <= 0) return 0;
    if (net_inb(io_base + REG_CR) & CR_BUFE) return 0;

    const uint32_t header = rx_offset;
    const uint16_t status = (uint16_t)(rx_buf[header] | (rx_buf[header + 1] << 8));
    const uint16_t length = (uint16_t)(rx_buf[header + 2] | (rx_buf[header + 3] << 8));

    if (!(status & 0x0001) || length < 4 || length > MAX_FRAME + 4) {
        rx_offset = 0;
        net_outw(io_base + REG_CAPR, (uint16_t)(0 - 16));
        return 0;
    }

    const int payload = length - 4;
    int copy = payload < max ? payload : max;
    for (int i = 0; i < copy; i++) out[i] = rx_buf[header + 4 + i];

    rx_offset = (header + length + 4 + 3) & ~3u;
    if (rx_offset >= RX_RING) rx_offset -= RX_RING;
    net_outw(io_base + REG_CAPR, (uint16_t)(rx_offset - 16));
    return copy;
}