#pragma once

#include <cpipe/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPIPE_NODE_PLUGIN_API_VERSION 1u

typedef struct cpipe_node_t cpipe_node_t;

typedef struct cpipe_host_api_t {
    uint32_t api_version;

    void (*log)(void* ctx, int level, const char* message);
    void* log_ctx;

    cpipe_status_t (*buffer_allocate)(void* ctx, cpipe_buffer_t* buf,
                                      uint32_t width, uint32_t height,
                                      cpipe_pixel_format_t format);
    void (*buffer_release)(void* ctx, cpipe_buffer_t* buf);
    void* buffer_ctx;
} cpipe_host_api_t;

cpipe_status_t cpipe_plugin_init(const cpipe_host_api_t* host);
void           cpipe_plugin_shutdown(void);

cpipe_node_t*  cpipe_node_create(const char* config_json);
void           cpipe_node_destroy(cpipe_node_t* node);

const cpipe_node_info_t* cpipe_node_get_info(const cpipe_node_t* node);
const char*              cpipe_node_get_parameter_schema(const cpipe_node_t* node);

cpipe_status_t cpipe_node_process(
    cpipe_node_t* node,
    const cpipe_buffer_t* const* inputs, uint32_t input_count,
    cpipe_buffer_t* const* outputs, uint32_t output_count,
    const char* params_json);

#ifdef __cplusplus
} /* extern "C" */
#endif
