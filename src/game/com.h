/* The COM methods the game's C calls (DirectDraw, Direct3D, DirectInput,
 * DirectPlay), by name and with their types.
 *
 * Objects are game memory addresses (uint32_t, as the game stores them).
 * A method is the function at its index in the object's vtable; vtable
 * entries are read at pointer width, so the same call works in the native
 * build (our objects, native/ddraw.c, dinput.c, dplayx.c) and in the
 * emulator (its stand-in objects), and stays right when pointers widen.
 *
 * The argument types follow native/'s implementations; methods native/
 * answers with a generic stub take DWORDs. Generated from native/; keep the
 * two in step. */
#ifndef REROLL_GAME_COM_H
#define REROLL_GAME_COM_H

#include "winapi.h"

#define COM_SELF(o) ((void *)(uintptr_t)(o))
#define COM_METHOD(o, index, type) ((type)(*(void *const *const *)COM_SELF(o))[index])

/* IDirectDraw */
static inline int32_t IDirectDraw_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}
static inline int32_t IDirectDraw_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDraw_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDraw_Compact(uint32_t self)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDraw_CreateClipper(uint32_t self, DWORD flags, void **out, void *outer)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, DWORD, void **, void *))(COM_SELF(self), flags, out, outer);
}
static inline int32_t IDirectDraw_CreatePalette(uint32_t self, DWORD flags, const void *entries, void **out, void *outer)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, DWORD, const void *, void **, void *))(COM_SELF(self), flags, entries, out, outer);
}
static inline int32_t IDirectDraw_CreateSurface(uint32_t self, const uint8_t *desc, void **out, void *outer)
{
    return COM_METHOD(self, 6, int32_t (W32API *)(void *, const uint8_t *, void **, void *))(COM_SELF(self), desc, out, outer);
}
static inline int32_t IDirectDraw_EnumDisplayModes(uint32_t self, DWORD flags, const uint8_t *filter, void *ctx, void *callback)
{
    return COM_METHOD(self, 8, int32_t (W32API *)(void *, DWORD, const uint8_t *, void *, void *))(COM_SELF(self), flags, filter, ctx, callback);
}
static inline int32_t IDirectDraw_FlipToGDISurface(uint32_t self)
{
    return COM_METHOD(self, 10, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDraw_GetCaps(uint32_t self, uint8_t *driver, uint8_t *hel)
{
    return COM_METHOD(self, 11, int32_t (W32API *)(void *, uint8_t *, uint8_t *))(COM_SELF(self), driver, hel);
}
static inline int32_t IDirectDraw_GetDisplayMode(uint32_t self, uint8_t *desc)
{
    return COM_METHOD(self, 12, int32_t (W32API *)(void *, uint8_t *))(COM_SELF(self), desc);
}
static inline int32_t IDirectDraw_RestoreDisplayMode(uint32_t self)
{
    return COM_METHOD(self, 19, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDraw_SetCooperativeLevel(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 20, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectDraw_SetDisplayMode(uint32_t self, DWORD w, DWORD h, DWORD bits)
{
    return COM_METHOD(self, 21, int32_t (W32API *)(void *, DWORD, DWORD, DWORD))(COM_SELF(self), w, h, bits);
}
static inline int32_t IDirectDraw_WaitForVerticalBlank(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 22, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectDraw2_GetAvailableVidMem(uint32_t self, const DWORD *caps, DWORD *total, DWORD *free_bytes)
{
    return COM_METHOD(self, 23, int32_t (W32API *)(void *, const DWORD *, DWORD *, DWORD *))(COM_SELF(self), caps, total, free_bytes);
}

/* IDirect3D2 */
static inline int32_t IDirect3D2_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}
static inline int32_t IDirect3D2_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3D2_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3D2_EnumDevices(uint32_t self, void *callback, void *ctx)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, void *, void *))(COM_SELF(self), callback, ctx);
}
static inline int32_t IDirect3D2_CreateMaterial(uint32_t self, void **out, void *outer)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, void **, void *))(COM_SELF(self), out, outer);
}
static inline int32_t IDirect3D2_CreateViewport(uint32_t self, void **out, void *outer)
{
    return COM_METHOD(self, 6, int32_t (W32API *)(void *, void **, void *))(COM_SELF(self), out, outer);
}
static inline int32_t IDirect3D2_CreateDevice(uint32_t self, const void *clsid, void *target, void **out)
{
    return COM_METHOD(self, 8, int32_t (W32API *)(void *, const void *, void *, void **))(COM_SELF(self), clsid, target, out);
}

/* IDirectDrawSurface */
static inline int32_t IDirectDrawSurface_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}
static inline int32_t IDirectDrawSurface_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDrawSurface_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDrawSurface_AddAttachedSurface(uint32_t self, void *other)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, void *))(COM_SELF(self), other);
}
static inline int32_t IDirectDrawSurface_Blt(uint32_t self, const int32_t *dst_rect, void *src, const int32_t *src_rect, DWORD flags, const uint8_t *fx)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, const int32_t *, void *, const int32_t *, DWORD, const uint8_t *))(COM_SELF(self), dst_rect, src, src_rect, flags, fx);
}
static inline int32_t IDirectDrawSurface_BltFast(uint32_t self, DWORD x, DWORD y, void *src, const int32_t *src_rect, DWORD flags)
{
    return COM_METHOD(self, 7, int32_t (W32API *)(void *, DWORD, DWORD, void *, const int32_t *, DWORD))(COM_SELF(self), x, y, src, src_rect, flags);
}
static inline int32_t IDirectDrawSurface_DeleteAttachedSurface(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 8, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectDrawSurface_Flip(uint32_t self, void *target, DWORD flags)
{
    return COM_METHOD(self, 11, int32_t (W32API *)(void *, void *, DWORD))(COM_SELF(self), target, flags);
}
static inline int32_t IDirectDrawSurface_GetAttachedSurface(uint32_t self, const DWORD *caps, void **out)
{
    return COM_METHOD(self, 12, int32_t (W32API *)(void *, const DWORD *, void **))(COM_SELF(self), caps, out);
}
static inline int32_t IDirectDrawSurface_GetBltStatus(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 13, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectDrawSurface_GetCaps(uint32_t self, DWORD *caps)
{
    return COM_METHOD(self, 14, int32_t (W32API *)(void *, DWORD *))(COM_SELF(self), caps);
}
static inline int32_t IDirectDrawSurface_GetColorKey(uint32_t self, DWORD flags, uint32_t *key)
{
    return COM_METHOD(self, 16, int32_t (W32API *)(void *, DWORD, uint32_t *))(COM_SELF(self), flags, key);
}
static inline int32_t IDirectDrawSurface_GetDC(uint32_t self, HANDLE *dc)
{
    return COM_METHOD(self, 17, int32_t (W32API *)(void *, HANDLE *))(COM_SELF(self), dc);
}
static inline int32_t IDirectDrawSurface_GetFlipStatus(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 18, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectDrawSurface_GetPixelFormat(uint32_t self, uint8_t *format)
{
    return COM_METHOD(self, 21, int32_t (W32API *)(void *, uint8_t *))(COM_SELF(self), format);
}
static inline int32_t IDirectDrawSurface_GetSurfaceDesc(uint32_t self, uint8_t *desc)
{
    return COM_METHOD(self, 22, int32_t (W32API *)(void *, uint8_t *))(COM_SELF(self), desc);
}
static inline int32_t IDirectDrawSurface_IsLost(uint32_t self)
{
    return COM_METHOD(self, 24, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDrawSurface_Lock(uint32_t self, const int32_t *rect, uint8_t *desc, DWORD flags, HANDLE event)
{
    return COM_METHOD(self, 25, int32_t (W32API *)(void *, const int32_t *, uint8_t *, DWORD, HANDLE))(COM_SELF(self), rect, desc, flags, event);
}
static inline int32_t IDirectDrawSurface_ReleaseDC(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 26, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectDrawSurface_Restore(uint32_t self)
{
    return COM_METHOD(self, 27, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDrawSurface_SetClipper(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 28, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectDrawSurface_SetColorKey(uint32_t self, DWORD flags, const uint32_t *key)
{
    return COM_METHOD(self, 29, int32_t (W32API *)(void *, DWORD, const uint32_t *))(COM_SELF(self), flags, key);
}
static inline int32_t IDirectDrawSurface_SetPalette(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 31, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectDrawSurface_Unlock(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 32, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectDrawSurface_PageLock(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 37, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectDrawSurface_PageUnlock(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 38, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}

/* IDirect3DTexture2 */
static inline int32_t IDirect3DTexture2_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}
static inline int32_t IDirect3DTexture2_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DTexture2_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DTexture2_GetHandle(uint32_t self, void *device, DWORD *handle)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, void *, DWORD *))(COM_SELF(self), device, handle);
}
static inline int32_t IDirect3DTexture2_PaletteChanged(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirect3DTexture2_Load(uint32_t self, void *source)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, void *))(COM_SELF(self), source);
}

/* IDirect3DDevice2 */
static inline int32_t IDirect3DDevice2_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DDevice2_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DDevice2_GetCaps(uint32_t self, void *hw, void *hel)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, void *, void *))(COM_SELF(self), hw, hel);
}
static inline int32_t IDirect3DDevice2_AddViewport(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 6, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirect3DDevice2_DeleteViewport(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 7, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirect3DDevice2_EnumTextureFormats(uint32_t self, void *callback, void *ctx)
{
    return COM_METHOD(self, 9, int32_t (W32API *)(void *, void *, void *))(COM_SELF(self), callback, ctx);
}
static inline int32_t IDirect3DDevice2_BeginScene(uint32_t self)
{
    return COM_METHOD(self, 10, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DDevice2_EndScene(uint32_t self)
{
    return COM_METHOD(self, 11, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DDevice2_SetCurrentViewport(uint32_t self, void *v)
{
    return COM_METHOD(self, 13, int32_t (W32API *)(void *, void *))(COM_SELF(self), v);
}
static inline int32_t IDirect3DDevice2_GetRenderState(uint32_t self, DWORD state, DWORD *value)
{
    return COM_METHOD(self, 22, int32_t (W32API *)(void *, DWORD, DWORD *))(COM_SELF(self), state, value);
}
static inline int32_t IDirect3DDevice2_SetRenderState(uint32_t self, DWORD state, DWORD value)
{
    return COM_METHOD(self, 23, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), state, value);
}
static inline int32_t IDirect3DDevice2_GetLightState(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 24, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirect3DDevice2_SetLightState(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 25, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirect3DDevice2_SetTransform(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 26, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirect3DDevice2_DrawPrimitive(uint32_t self, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e)
{
    return COM_METHOD(self, 29, int32_t (W32API *)(void *, DWORD, DWORD, DWORD, DWORD, DWORD))(COM_SELF(self), a, b, c, d, e);
}
static inline int32_t IDirect3DDevice2_DrawIndexedPrimitive(uint32_t self, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g)
{
    return COM_METHOD(self, 30, int32_t (W32API *)(void *, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD))(COM_SELF(self), a, b, c, d, e, f, g);
}
static inline int32_t IDirect3DDevice2_SetClipStatus(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 31, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirect3DDevice2_GetClipStatus(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 32, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}

/* IDirect3DViewport2 */
static inline int32_t IDirect3DViewport2_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DViewport2_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DViewport2_GetViewport(uint32_t self, uint8_t *data)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, uint8_t *))(COM_SELF(self), data);
}
static inline int32_t IDirect3DViewport2_SetViewport(uint32_t self, const uint8_t *data)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, const uint8_t *))(COM_SELF(self), data);
}
static inline int32_t IDirect3DViewport2_SetBackground(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 8, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirect3DViewport2_Clear(uint32_t self, DWORD a, DWORD b, DWORD c)
{
    return COM_METHOD(self, 12, int32_t (W32API *)(void *, DWORD, DWORD, DWORD))(COM_SELF(self), a, b, c);
}
static inline int32_t IDirect3DViewport2_GetViewport2(uint32_t self, uint8_t *data)
{
    return COM_METHOD(self, 16, int32_t (W32API *)(void *, uint8_t *))(COM_SELF(self), data);
}
static inline int32_t IDirect3DViewport2_SetViewport2(uint32_t self, const uint8_t *data)
{
    return COM_METHOD(self, 17, int32_t (W32API *)(void *, const uint8_t *))(COM_SELF(self), data);
}

/* IDirect3DMaterial2 */
static inline int32_t IDirect3DMaterial2_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DMaterial2_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirect3DMaterial2_SetMaterial(uint32_t self, const uint8_t *data)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, const uint8_t *))(COM_SELF(self), data);
}
static inline int32_t IDirect3DMaterial2_GetHandle(uint32_t self, void *device, DWORD *handle)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, void *, DWORD *))(COM_SELF(self), device, handle);
}

/* IDirectDrawClipper */
static inline int32_t IDirectDrawClipper_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDrawClipper_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDrawClipper_SetHWnd(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 8, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}

/* IDirectDrawPalette */
static inline int32_t IDirectDrawPalette_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDrawPalette_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectDrawPalette_SetEntries(uint32_t self, DWORD a, DWORD b, DWORD c, DWORD d)
{
    return COM_METHOD(self, 6, int32_t (W32API *)(void *, DWORD, DWORD, DWORD, DWORD))(COM_SELF(self), a, b, c, d);
}

/* IDirectInputA */
static inline int32_t IDirectInputA_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}
static inline int32_t IDirectInputA_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputA_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputA_CreateDevice(uint32_t self, const uint8_t *guid, void **out, void *outer)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, const uint8_t *, void **, void *))(COM_SELF(self), guid, out, outer);
}
static inline int32_t IDirectInputA_EnumDevices(uint32_t self, DWORD type, void *callback, void *ctx, DWORD flags)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, DWORD, void *, void *, DWORD))(COM_SELF(self), type, callback, ctx, flags);
}

/* IDirectInputDevice2A */
static inline int32_t IDirectInputDevice2A_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}
static inline int32_t IDirectInputDevice2A_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputDevice2A_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputDevice2A_GetCapabilities(uint32_t self, DWORD *caps)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, DWORD *))(COM_SELF(self), caps);
}
static inline int32_t IDirectInputDevice2A_EnumObjects(uint32_t self, void *callback, void *ctx, DWORD flags)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, void *, void *, DWORD))(COM_SELF(self), callback, ctx, flags);
}
static inline int32_t IDirectInputDevice2A_GetProperty(uint32_t self, const void *prop, DWORD *header)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, const void *, DWORD *))(COM_SELF(self), prop, header);
}
static inline int32_t IDirectInputDevice2A_SetProperty(uint32_t self, const void *prop, const DWORD *header)
{
    return COM_METHOD(self, 6, int32_t (W32API *)(void *, const void *, const DWORD *))(COM_SELF(self), prop, header);
}
static inline int32_t IDirectInputDevice2A_Acquire(uint32_t self)
{
    return COM_METHOD(self, 7, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputDevice2A_Unacquire(uint32_t self)
{
    return COM_METHOD(self, 8, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputDevice2A_GetDeviceState(uint32_t self, DWORD size, uint8_t *data)
{
    return COM_METHOD(self, 9, int32_t (W32API *)(void *, DWORD, uint8_t *))(COM_SELF(self), size, data);
}
static inline int32_t IDirectInputDevice2A_SetDataFormat(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 11, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectInputDevice2A_SetEventNotification(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 12, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectInputDevice2A_SetCooperativeLevel(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 13, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectInputDevice2A_GetDeviceInfo(uint32_t self, uint8_t *info)
{
    return COM_METHOD(self, 15, int32_t (W32API *)(void *, uint8_t *))(COM_SELF(self), info);
}
static inline int32_t IDirectInputDevice2A_CreateEffect(uint32_t self, const uint8_t *guid, const uint8_t *params, void **out, void *outer)
{
    return COM_METHOD(self, 18, int32_t (W32API *)(void *, const uint8_t *, const uint8_t *, void **, void *))(COM_SELF(self), guid, params, out, outer);
}
static inline int32_t IDirectInputDevice2A_GetForceFeedbackState(uint32_t self, DWORD *state)
{
    return COM_METHOD(self, 21, int32_t (W32API *)(void *, DWORD *))(COM_SELF(self), state);
}
static inline int32_t IDirectInputDevice2A_SendForceFeedbackCommand(uint32_t self, DWORD command)
{
    return COM_METHOD(self, 22, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), command);
}
static inline int32_t IDirectInputDevice2A_Poll(uint32_t self)
{
    return COM_METHOD(self, 25, int32_t (W32API *)(void *))(COM_SELF(self));
}

/* IDirectInputEffect */
static inline int32_t IDirectInputEffect_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}
static inline int32_t IDirectInputEffect_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputEffect_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputEffect_GetEffectGuid(uint32_t self, uint8_t *guid)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, uint8_t *))(COM_SELF(self), guid);
}
static inline int32_t IDirectInputEffect_SetParameters(uint32_t self, const uint8_t *params, DWORD flags)
{
    return COM_METHOD(self, 6, int32_t (W32API *)(void *, const uint8_t *, DWORD))(COM_SELF(self), params, flags);
}
static inline int32_t IDirectInputEffect_Start(uint32_t self, DWORD iterations, DWORD flags)
{
    return COM_METHOD(self, 7, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), iterations, flags);
}
static inline int32_t IDirectInputEffect_Stop(uint32_t self)
{
    return COM_METHOD(self, 8, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputEffect_GetEffectStatus(uint32_t self, DWORD *status)
{
    return COM_METHOD(self, 9, int32_t (W32API *)(void *, DWORD *))(COM_SELF(self), status);
}
static inline int32_t IDirectInputEffect_Download(uint32_t self)
{
    return COM_METHOD(self, 10, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectInputEffect_Unload(uint32_t self)
{
    return COM_METHOD(self, 11, int32_t (W32API *)(void *))(COM_SELF(self));
}

/* IDirectPlay4A */
static inline int32_t IDirectPlay4A_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}
static inline int32_t IDirectPlay4A_AddRef(uint32_t self)
{
    return COM_METHOD(self, 1, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectPlay4A_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectPlay4A_Close(uint32_t self)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *))(COM_SELF(self));
}
static inline int32_t IDirectPlay4A_CreatePlayer(uint32_t self, DWORD *id, void *name, void *event, void *data, DWORD size, DWORD flags)
{
    return COM_METHOD(self, 6, int32_t (W32API *)(void *, DWORD *, void *, void *, void *, DWORD, DWORD))(COM_SELF(self), id, name, event, data, size, flags);
}
static inline int32_t IDirectPlay4A_DestroyPlayer(uint32_t self, DWORD a)
{
    return COM_METHOD(self, 9, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), a);
}
static inline int32_t IDirectPlay4A_EnumSessions(uint32_t self, void *desc, DWORD timeout, void *callback, void *ctx, DWORD flags)
{
    return COM_METHOD(self, 13, int32_t (W32API *)(void *, void *, DWORD, void *, void *, DWORD))(COM_SELF(self), desc, timeout, callback, ctx, flags);
}
static inline int32_t IDirectPlay4A_GetCaps(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 14, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectPlay4A_GetMessageCount(uint32_t self, DWORD player, DWORD *count)
{
    return COM_METHOD(self, 17, int32_t (W32API *)(void *, DWORD, DWORD *))(COM_SELF(self), player, count);
}
static inline int32_t IDirectPlay4A_GetPlayerAddress(uint32_t self, DWORD a, DWORD b, DWORD c)
{
    return COM_METHOD(self, 18, int32_t (W32API *)(void *, DWORD, DWORD, DWORD))(COM_SELF(self), a, b, c);
}
static inline int32_t IDirectPlay4A_GetSessionDesc(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 22, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectPlay4A_Open(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 24, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectPlay4A_Receive(uint32_t self, DWORD *from, DWORD *to, DWORD flags, void *data, DWORD *size)
{
    return COM_METHOD(self, 25, int32_t (W32API *)(void *, DWORD *, DWORD *, DWORD, void *, DWORD *))(COM_SELF(self), from, to, flags, data, size);
}
static inline int32_t IDirectPlay4A_SetSessionDesc(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 31, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectPlay4A_EnumConnections(uint32_t self, const void *app, void *callback, void *ctx, DWORD flags)
{
    return COM_METHOD(self, 35, int32_t (W32API *)(void *, const void *, void *, void *, DWORD))(COM_SELF(self), app, callback, ctx, flags);
}
static inline int32_t IDirectPlay4A_InitializeConnection(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 38, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectPlay4A_SendChatMessage(uint32_t self, DWORD a, DWORD b, DWORD c, DWORD e)
{
    return COM_METHOD(self, 40, int32_t (W32API *)(void *, DWORD, DWORD, DWORD, DWORD))(COM_SELF(self), a, b, c, e);
}
static inline int32_t IDirectPlay4A_GetPlayerFlags(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 46, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}
static inline int32_t IDirectPlay4A_SendEx(uint32_t self, DWORD from, DWORD to, DWORD flags, void *data, DWORD size, DWORD priority, DWORD timeout, void *context, DWORD *id)
{
    return COM_METHOD(self, 49, int32_t (W32API *)(void *, DWORD, DWORD, DWORD, void *, DWORD, DWORD, DWORD, void *, DWORD *))(COM_SELF(self), from, to, flags, data, size, priority, timeout, context, id);
}
static inline int32_t IDirectPlay4A_GetMessageQueue(uint32_t self, DWORD from, DWORD to, DWORD flags, DWORD *messages, DWORD *bytes)
{
    return COM_METHOD(self, 50, int32_t (W32API *)(void *, DWORD, DWORD, DWORD, DWORD *, DWORD *))(COM_SELF(self), from, to, flags, messages, bytes);
}
static inline int32_t IDirectPlay4A_CancelMessage(uint32_t self, DWORD a, DWORD b)
{
    return COM_METHOD(self, 51, int32_t (W32API *)(void *, DWORD, DWORD))(COM_SELF(self), a, b);
}

/* IDirectDraw2: as IDirectDraw, but SetDisplayMode takes a refresh rate and flags */
static inline int32_t IDirectDraw2_SetDisplayMode(uint32_t self, DWORD w, DWORD h, DWORD bits, DWORD rate, DWORD flags)
{
    return COM_METHOD(self, 21, int32_t (W32API *)(void *, DWORD, DWORD, DWORD, DWORD, DWORD))(COM_SELF(self), w, h, bits, rate, flags);
}

/* Methods native/ has no object for (DirectSound: DirectSoundCreate fails;
 * the lobby: DirectPlayLobbyCreateA fails) or answers with its stub: the
 * game's calls, as it makes them. */

/* IDirectDraw4 */
static inline int32_t IDirectDraw4_GetDeviceIdentifier(uint32_t self, uint8_t *ident, DWORD flags)
{
    return COM_METHOD(self, 27, int32_t (W32API *)(void *, uint8_t *, DWORD))(COM_SELF(self), ident, flags);
}

/* IDirectDrawSurface */
static inline int32_t IDirectDrawSurface_GetPalette(uint32_t self, void **out)
{
    return COM_METHOD(self, 20, int32_t (W32API *)(void *, void **))(COM_SELF(self), out);
}

/* IDirectDrawPalette */
static inline int32_t IDirectDrawPalette_GetEntries(uint32_t self, DWORD flags, DWORD base, DWORD count,
                                                       void *entries)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, DWORD, DWORD, DWORD, void *))(COM_SELF(self), flags, base,
                                                                                         count, entries);
}

/* IDirectSound */
static inline int32_t IDirectSound_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}

static inline int32_t IDirectSound_CreateSoundBuffer(uint32_t self, const DWORD *desc, void **out, void *outer)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, const DWORD *, void **, void *))(COM_SELF(self), desc, out,
                                                                                           outer);
}

static inline int32_t IDirectSound_GetCaps(uint32_t self, DWORD *caps)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, DWORD *))(COM_SELF(self), caps);
}

static inline int32_t IDirectSound_SetCooperativeLevel(uint32_t self, HWND window, DWORD level)
{
    return COM_METHOD(self, 6, int32_t (W32API *)(void *, HWND, DWORD))(COM_SELF(self), window, level);
}

/* IDirectSoundBuffer */
static inline int32_t IDirectSoundBuffer_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}

static inline int32_t IDirectSoundBuffer_GetCaps(uint32_t self, DWORD *caps)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, DWORD *))(COM_SELF(self), caps);
}

static inline int32_t IDirectSoundBuffer_GetCurrentPosition(uint32_t self, DWORD *play, DWORD *write)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, DWORD *, DWORD *))(COM_SELF(self), play, write);
}

static inline int32_t IDirectSoundBuffer_Lock(uint32_t self, DWORD offset, DWORD bytes, void **p1, DWORD *n1,
                                              void **p2, DWORD *n2, DWORD flags)
{
    return COM_METHOD(self, 11, int32_t (W32API *)(void *, DWORD, DWORD, void **, DWORD *, void **, DWORD *,
                                                   DWORD))(COM_SELF(self), offset, bytes, p1, n1, p2, n2, flags);
}

static inline int32_t IDirectSoundBuffer_Play(uint32_t self, DWORD reserved, DWORD priority, DWORD flags)
{
    return COM_METHOD(self, 12, int32_t (W32API *)(void *, DWORD, DWORD, DWORD))(COM_SELF(self), reserved, priority,
                                                                                 flags);
}

static inline int32_t IDirectSoundBuffer_SetFormat(uint32_t self, const uint8_t *wfx)
{
    return COM_METHOD(self, 14, int32_t (W32API *)(void *, const uint8_t *))(COM_SELF(self), wfx);
}

static inline int32_t IDirectSoundBuffer_SetFrequency(uint32_t self, DWORD frequency)
{
    return COM_METHOD(self, 17, int32_t (W32API *)(void *, DWORD))(COM_SELF(self), frequency);
}

static inline int32_t IDirectSoundBuffer_Unlock(uint32_t self, void *p1, DWORD n1, void *p2, DWORD n2)
{
    return COM_METHOD(self, 19, int32_t (W32API *)(void *, void *, DWORD, void *, DWORD))(COM_SELF(self), p1, n1, p2,
                                                                                          n2);
}

static inline int32_t IDirectSoundBuffer_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}

/* IKsPropertySet */
static inline int32_t IKsPropertySet_Get(uint32_t self, const void *set, DWORD id, void *instance, DWORD instance_size,
                                         void *data, DWORD size, DWORD *returned)
{
    return COM_METHOD(self, 3, int32_t (W32API *)(void *, const void *, DWORD, void *, DWORD, void *, DWORD, DWORD *))(
        COM_SELF(self), set, id, instance, instance_size, data, size, returned);
}

static inline int32_t IKsPropertySet_Set(uint32_t self, const void *set, DWORD id, void *instance, DWORD instance_size,
                                         void *data, DWORD size)
{
    return COM_METHOD(self, 4, int32_t (W32API *)(void *, const void *, DWORD, void *, DWORD, void *, DWORD))(
        COM_SELF(self), set, id, instance, instance_size, data, size);
}

static inline int32_t IKsPropertySet_QuerySupport(uint32_t self, const void *set, DWORD id, DWORD *support)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, const void *, DWORD, DWORD *))(COM_SELF(self), set, id,
                                                                                          support);
}

/* IDirectPlayLobbyA */
static inline int32_t IDirectPlayLobbyA_QueryInterface(uint32_t self, const void *iid, void **out)
{
    return COM_METHOD(self, 0, int32_t (W32API *)(void *, const void *, void **))(COM_SELF(self), iid, out);
}

static inline int32_t IDirectPlayLobbyA_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}

/* IDirectPlayLobby2A */
static inline int32_t IDirectPlayLobby2A_Release(uint32_t self)
{
    return COM_METHOD(self, 2, int32_t (W32API *)(void *))(COM_SELF(self));
}

static inline int32_t IDirectPlayLobby2A_EnumAddress(uint32_t self, void *callback, const void *address, DWORD size,
                                                     void *ctx)
{
    return COM_METHOD(self, 5, int32_t (W32API *)(void *, void *, const void *, DWORD, void *))(
        COM_SELF(self), callback, address, size, ctx);
}

static inline int32_t IDirectPlayLobby2A_CreateCompoundAddress(uint32_t self, const void *elements, DWORD count,
                                                               void *address, DWORD *size)
{
    return COM_METHOD(self, 14, int32_t (W32API *)(void *, const void *, DWORD, void *, DWORD *))(
        COM_SELF(self), elements, count, address, size);
}

#endif
