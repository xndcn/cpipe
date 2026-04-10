/* SPDX-License-Identifier: MIT */
/* cpipe/types.h -- C-compatible public type definitions
 * Compiles as both C11 and C++20. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ──────────────────────────────────────────────────────────── */
typedef enum cpipe_status_t {
    CPIPE_STATUS_OK = 0,
    CPIPE_STATUS_ERROR_INVALID_PARAM,
    CPIPE_STATUS_ERROR_OUT_OF_MEMORY,
    CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
    CPIPE_STATUS_ERROR_IO,
    CPIPE_STATUS_ERROR_UNSUPPORTED,
    CPIPE_STATUS_ERROR_ABI_MISMATCH,
} cpipe_status_t;

/* ── Pixel formats ────────────────────────────────────────────────────────── */
typedef enum cpipe_pixel_format_t {
    CPIPE_PIXEL_FORMAT_BAYER_RGGB_16, /* 16-bit Bayer RGGB */
    CPIPE_PIXEL_FORMAT_BAYER_BGGR_16, /* 16-bit Bayer BGGR */
    CPIPE_PIXEL_FORMAT_BAYER_GRBG_16, /* 16-bit Bayer GRBG */
    CPIPE_PIXEL_FORMAT_BAYER_GBRG_16, /* 16-bit Bayer GBRG */
    CPIPE_PIXEL_FORMAT_RGB_16,        /* 16-bit RGB planar/packed */
    CPIPE_PIXEL_FORMAT_RGB_8,         /* 8-bit RGB  */
    CPIPE_PIXEL_FORMAT_RGBA_8,        /* 8-bit RGBA */
    CPIPE_PIXEL_FORMAT_RGB_FLOAT32,   /* 32-bit float RGB */
} cpipe_pixel_format_t;

/* ── Device types (power-of-two for use as bitmask in supported_devices) ── */
typedef enum cpipe_device_type_t {
    CPIPE_DEVICE_CPU = 1,
    CPIPE_DEVICE_GPU = 2,
    CPIPE_DEVICE_NPU = 4,
} cpipe_device_type_t;

/* ── Buffer descriptor (passed across C ABI) ─────────────────────────────── */
typedef struct cpipe_buffer_t {
    uint32_t             width;   /* pixels */
    uint32_t             height;  /* pixels */
    cpipe_pixel_format_t format;
    uint32_t             stride;  /* bytes per row (>= width * bytes_per_pixel) */
    cpipe_device_type_t  device;
    void*                data;    /* opaque pixel data pointer */
    uint64_t             size;    /* total bytes (stride * height) */
} cpipe_buffer_t;

/* ── Node metadata ────────────────────────────────────────────────────────── */
typedef struct cpipe_node_info_t {
    const char* plugin_id;         /* e.g. "cpipe.isp.demosaic" */
    const char* display_name;      /* e.g. "Demosaic (Malvar)" */
    const char* version;           /* semver, e.g. "1.0.0" */
    uint32_t    abi_version;       /* must match host ABI version */
    uint32_t    input_count;
    uint32_t    output_count;
    uint32_t    supported_devices; /* bitmask of cpipe_device_type_t values */
    const char* category;          /* e.g. "isp.preprocessing" */
} cpipe_node_info_t;

#ifdef __cplusplus
} /* extern "C" */
#endif
