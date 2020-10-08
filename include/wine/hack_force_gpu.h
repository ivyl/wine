/* GPU Faking
 *
 * Copyright 2020 Arkadiusz Hiler for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdlib.h>
#include "windef.h"

WINE_DECLARE_DEBUG_CHANNEL(forcegpu);

#define USE_DXVK_CONFIG TRUE

#define HACK_VENDOR_AMD    0x1002
#define HACK_DEV_RX480     0x67df
#define HACK_VENDOR_NVIDIA 0x10de
#define HACK_DEV_1050TI    0x1c82
#define HACK_VENDOR_INTEL  0x8086
#define HACK_DEV_IRIS_P580  0x193b

/* from dxvk_config.h, not available at wine build time in Proton */
struct DXVKOptions {
        int32_t customVendorId;
        int32_t customDeviceId;
        int32_t nvapiHack;
};

struct DXVK {
    BOOL is_initialized;
    struct DXVKOptions options;
};

static BOOL WINAPI hack_fake_gpu_load_dxvk_config(INIT_ONCE *once, void *param, void **context)
{
    static HMODULE dxvk_config_mod;
    struct DXVK *dxvk = param;
    HRESULT (WINAPI *pDXVKGetOptions)(struct DXVKOptions *out_opts);

    dxvk_config_mod = LoadLibraryA("dxvk_config.dll");
    if(!dxvk_config_mod)
    {
        ERR_(forcegpu)("Couldn't load dxvk_config.dll, won't apply default DXVK config options\n");
        return TRUE;
    }

    pDXVKGetOptions = (void*)GetProcAddress(dxvk_config_mod, "DXVKGetOptions");
    if(!pDXVKGetOptions)
    {
        ERR_(forcegpu)("dxvk_config doesn't have DXVKGetOptions?!\n");
        return TRUE;
    }

    if (pDXVKGetOptions(&dxvk->options) == S_OK)
    {
        TRACE_(forcegpu)("got dxvk options:\n");
        TRACE_(forcegpu)("\tnvapiHack: %u\n", dxvk->options.nvapiHack);
        TRACE_(forcegpu)("\tcustomVendorId: 0x%04x\n", dxvk->options.customVendorId);
        TRACE_(forcegpu)("\tcustomDeviceId: 0x%04x\n", dxvk->options.customDeviceId);
        dxvk->is_initialized = TRUE;
    }
    else
        WARN_(forcegpu)("failed to get DXVK options!\n");
    return TRUE;
}

static void HACK_force_gpu(UINT *vendor, UINT *device, BOOL use_dxvk_config)
{
    static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;
    UINT new_vendor = *vendor, new_device = *device;
    static struct DXVK dxvk;
    const char *sgi;

    if (use_dxvk_config)
    {
        InitOnceExecuteOnce(&init_once, hack_fake_gpu_load_dxvk_config, &dxvk, NULL);
        if (dxvk.is_initialized)
        {
            /* logic from dxvk/src/dxgi/dxgi_adapter.cpp:DxgiAdapter::GetDesc2 */
            if (dxvk.options.customVendorId >= 0)
                new_vendor = dxvk.options.customVendorId;

            if (dxvk.options.customDeviceId >= 0)
                new_device = dxvk.options.customDeviceId;

            if (dxvk.options.customVendorId < 0 && dxvk.options.customDeviceId < 0 &&
                    dxvk.options.nvapiHack && *vendor == HACK_VENDOR_NVIDIA) {
                TRACE_(forcegpu)("NvAPI workaround enabled, reporting AMD GPU\n");
                new_vendor = HACK_VENDOR_AMD;
                new_device = HACK_DEV_RX480;
            }
        }
    }

    if ((sgi = getenv("WINE_FORCE_GPU_IF")))
    {
        if (*vendor != HACK_VENDOR_AMD    && !strcmp("AMD",    sgi)) goto out;
        if (*vendor != HACK_VENDOR_NVIDIA && !strcmp("NVIDIA", sgi)) goto out;
        if (*vendor != HACK_VENDOR_INTEL  && !strcmp("INTEL",  sgi)) goto out;
    }

    if ((sgi = getenv("WINE_FORCE_GPU")))
    {
        if (!strcmp("AMD", sgi))
        {
            if (*vendor != HACK_VENDOR_AMD)
            {
                new_vendor = HACK_VENDOR_AMD;
                new_device = HACK_DEV_RX480;
            }
        }
        else if (!strcmp("NVIDIA", sgi))
        {
            if (*vendor != HACK_VENDOR_NVIDIA)
            {
                new_vendor = HACK_VENDOR_NVIDIA;
                new_device = HACK_DEV_1050TI;
            }
        }
        else if (!strcmp("INTEL", sgi))
        {
            if (*vendor != HACK_VENDOR_INTEL)
            {
                new_vendor = HACK_VENDOR_INTEL;
                new_device = HACK_DEV_IRIS_P580;
            }
        }
    }

out:
    TRACE_(forcegpu)("Reporting vendor: %x, device: %x\n", new_vendor, new_device);
    *vendor = new_vendor;
    *device = new_device;
}

#undef HACK_DEV_IRIS_P580
#undef HACK_VENDOR_INTEL
#undef HACK_DEV_1050TI
#undef HACK_VENDOR_NVIDIA
#undef HACK_DEV_RX480
#undef HACK_VENDOR_AMD
