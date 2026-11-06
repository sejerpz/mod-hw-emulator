#include "module_loader.h"

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

const mhe_module *mhe_load_module(const char *path, const mhe_host *host, char *error, size_t error_size)
{
    mhe_module_entry_fn entry = NULL;

#ifdef _WIN32
    HMODULE lib = LoadLibraryA(path);
    if (! lib)
    {
        snprintf(error, error_size, "cannot load %s (error %lu)", path, (unsigned long)GetLastError());
        return NULL;
    }
    entry = (mhe_module_entry_fn)(void *)GetProcAddress(lib, MHE_MODULE_ENTRY);
#else
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (! lib)
    {
        snprintf(error, error_size, "%s", dlerror());
        return NULL;
    }
    *(void **)&entry = dlsym(lib, MHE_MODULE_ENTRY);
#endif

    if (! entry)
    {
        snprintf(error, error_size, "%s has no %s", path, MHE_MODULE_ENTRY);
        return NULL;
    }

    const mhe_module *module = entry(host);
    if (! module)
        snprintf(error, error_size, "%s refused the host", path);
    return module;
}
