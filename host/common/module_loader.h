#ifndef MHE_MODULE_LOADER_H
#define MHE_MODULE_LOADER_H

#include "mhe/emu_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* loads a firmware module and returns its interface, NULL on error (message in `error`) */
const mhe_module *mhe_load_module(const char *path, const mhe_host *host, char *error, size_t error_size);

/* the firmware info of a module, without starting it; NULL when the module cannot be loaded or has none */
const mhe_fw_info *mhe_module_info(const char *path);

/* the firmware info of a loaded module, NULL when it has none */
const mhe_fw_info *mhe_module_firmware(const mhe_module *module);

/* the state folder of a firmware when the user names none: BASE/<tag> (e.g. mhe-state/stls18), BASE without
 * info; characters that do not belong in a folder name become '_' */
void mhe_default_state_dir(const char *base, const mhe_fw_info *info, char *dir, size_t dir_size);

/* creates a folder and its parents; 0 on success or when it is there */
int mhe_mkdir_p(const char *dir);

#ifdef __cplusplus
}
#endif

#endif
