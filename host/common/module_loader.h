#ifndef MHE_MODULE_LOADER_H
#define MHE_MODULE_LOADER_H

#include "mhe/emu_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* loads a firmware module and returns its interface, NULL on error (message in `error`) */
const mhe_module *mhe_load_module(const char *path, const mhe_host *host, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
