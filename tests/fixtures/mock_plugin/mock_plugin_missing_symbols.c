#include <cpipe/node_plugin.h>

#include <stdint.h>
#include <stdlib.h>

struct cpipe_node_t {
    uint32_t magic;
};

static const cpipe_node_info_t k_node_info = {
    .plugin_id = "cpipe.test.mock.missing_symbols",
    .display_name = "Mock Plugin Missing Symbols",
    .version = "1.0.0",
    .abi_version = CPIPE_NODE_PLUGIN_API_VERSION,
    .input_count = 1,
    .output_count = 1,
    .supported_devices = CPIPE_DEVICE_CPU,
    .category = "test",
};

cpipe_status_t cpipe_plugin_init(const cpipe_host_api_t* host) {
    (void)host;
    return CPIPE_STATUS_OK;
}

void cpipe_plugin_shutdown(void) {}

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
