#include <cpipe/node_plugin.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct cpipe_node_t {
    uint32_t magic;
};

static const cpipe_host_api_t* g_host = NULL;

static const cpipe_node_info_t k_node_info = {
    .plugin_id = "cpipe.test.mock",
    .display_name = "Mock Plugin",
    .version = "1.0.0",
    .abi_version = CPIPE_NODE_PLUGIN_API_VERSION,
    .input_count = 1,
    .output_count = 1,
    .supported_devices = CPIPE_DEVICE_CPU,
    .category = "test",
};

cpipe_status_t cpipe_plugin_init(const cpipe_host_api_t* host) {
    g_host = host;
    if (host != NULL && host->log != NULL) {
        host->log(host->log_ctx, 2, "mock_plugin initialized");
    }
    return CPIPE_STATUS_OK;
}

void cpipe_plugin_shutdown(void) {
    if (g_host != NULL && g_host->log != NULL) {
        g_host->log(g_host->log_ctx, 2, "mock_plugin shutdown");
    }
    g_host = NULL;
}

cpipe_node_t* cpipe_node_create(const char* config_json) {
    (void)config_json;

    cpipe_node_t* node = (cpipe_node_t*)malloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->magic = 0x43504950u;
    return node;
}

void cpipe_node_destroy(cpipe_node_t* node) {
    free(node);
}

const cpipe_node_info_t* cpipe_node_get_info(const cpipe_node_t* node) {
    if (node == NULL) {
        return NULL;
    }
    return &k_node_info;
}

const char* cpipe_node_get_parameter_schema(const cpipe_node_t* node) {
    if (node == NULL) {
        return NULL;
    }
    return "{}";
}

cpipe_status_t cpipe_node_process(
    cpipe_node_t* node,
    const cpipe_buffer_t* const* inputs, uint32_t input_count,
    cpipe_buffer_t* const* outputs, uint32_t output_count,
    const char* params_json) {
    (void)params_json;

    if (node == NULL || inputs == NULL || outputs == NULL || input_count != 1 || output_count != 1 ||
        inputs[0] == NULL || outputs[0] == NULL || inputs[0]->data == NULL || outputs[0]->data == NULL) {
        return CPIPE_STATUS_ERROR_INVALID_PARAM;
    }

    const uint64_t input_size = inputs[0]->size;
    const uint64_t output_size = outputs[0]->size;
    const uint64_t bytes = input_size < output_size ? input_size : output_size;
    memcpy(outputs[0]->data, inputs[0]->data, (size_t)bytes);
    return CPIPE_STATUS_OK;
}
