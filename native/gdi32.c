/* GDI32 for the native build: the game only asks the display's colour depth
 * and makes a palette for 8-bit displays. The fake display is 16-bit. */
#include "nt.h"

HANDLE WINAPI CreateICA(const char *driver, const char *device, const char *port, const void *mode)
{
    (void)driver;
    (void)device;
    (void)port;
    (void)mode;
    return (HANDLE)0x40001;
}

BOOL WINAPI DeleteDC(HANDLE dc)
{
    (void)dc;
    return TRUE;
}

int WINAPI GetDeviceCaps(HANDLE dc, int index)
{
    (void)dc;
    switch (index) {
    case 12: return 16;   /* BITSPIXEL */
    case 14: return 1;    /* PLANES */
    case 8: return 1024;  /* HORZRES */
    case 10: return 768;  /* VERTRES */
    default: return 0;
    }
}

HANDLE WINAPI CreatePalette(const void *palette)
{
    (void)palette;
    return (HANDLE)0x40002;
}

HANDLE WINAPI GetStockObject(int index)
{
    return (HANDLE)(uintptr_t)(uint32_t)(0x40100 + index);
}
