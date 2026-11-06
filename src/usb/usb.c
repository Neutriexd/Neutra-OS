#include "usb.h"
#include "../kernel/vga.h"
#include "../kernel/kernel.h"

uint32_t usb_init(void)
{
    vga_write(
        "USB: Controller detection not implemented\n",
        MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_YELLOW)
    );

    return 0;
}
