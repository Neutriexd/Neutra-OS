#include "net.h"
#include "rtl8139.h"
#include "vga.h"
#include "kernel.h"

static int driver_status;

int net_init(void) {
    driver_status = rtl8139_init();
    return driver_status;
}

int net_ready(void) {
    return rtl8139_ready();
}

void net_mac(uint8_t out[6]) {
    rtl8139_mac(out);
}

int net_send(const uint8_t* frame, int length) {
    return rtl8139_send(frame, length);
}

int net_receive(uint8_t* out, int max) {
    return rtl8139_receive(out, max);
}

static void put_hex_byte(uint8_t value, unsigned char color) {
    const char* digits = "0123456789abcdef";
    vga_putchar(digits[value >> 4], color);
    vga_putchar(digits[value & 15], color);
}

void net_print_info(void) {
    const unsigned char color = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    if (!net_ready()) {
        vga_write("net: no network card (init status ", MAKE_COLOR(COLOR_BLACK, COLOR_RED));
        vga_print_int(driver_status, MAKE_COLOR(COLOR_BLACK, COLOR_RED));
        vga_write(")\n", MAKE_COLOR(COLOR_BLACK, COLOR_RED));
        return;
    }
    uint8_t mac[6];
    net_mac(mac);
    vga_write("net: RTL8139 ready, MAC ", color);
    for (int i = 0; i < 6; i++) {
        put_hex_byte(mac[i], color);
        if (i < 5) vga_putchar(':', color);
    }
    vga_putchar('\n', color);
}