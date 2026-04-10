// src/common/error.cpp
#include "error.h"

namespace cpipe {

std::string_view status_to_string(cpipe_status_t code) noexcept {
    switch (code) {
    case CPIPE_STATUS_OK:                       return "OK";
    case CPIPE_STATUS_ERROR_INVALID_PARAM:      return "ERROR_INVALID_PARAM";
    case CPIPE_STATUS_ERROR_OUT_OF_MEMORY:      return "ERROR_OUT_OF_MEMORY";
    case CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED: return "ERROR_PLUGIN_LOAD_FAILED";
    case CPIPE_STATUS_ERROR_IO:                 return "ERROR_IO";
    case CPIPE_STATUS_ERROR_UNSUPPORTED:        return "ERROR_UNSUPPORTED";
    case CPIPE_STATUS_ERROR_ABI_MISMATCH:       return "ERROR_ABI_MISMATCH";
    }
    return "ERROR_UNKNOWN";
}

} // namespace cpipe
