#include "module_loader.h"

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/stat.h>
#endif

/* the library stays loaded: the firmware threads live until the process exits */
static void *open_library(const char *path, char *error, size_t error_size)
{
#ifdef _WIN32
    HMODULE lib = LoadLibraryA(path);
    if (! lib)
        snprintf(error, error_size, "cannot load %s (error %lu)", path, (unsigned long)GetLastError());
    return (void *)lib;
#else
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (! lib)
        snprintf(error, error_size, "%s", dlerror());
    return lib;
#endif
}

static void *find_symbol(void *lib, const char *name)
{
    void *symbol;
#ifdef _WIN32
    *(FARPROC *)&symbol = GetProcAddress((HMODULE)lib, name);
#else
    symbol = dlsym(lib, name);
#endif
    return symbol;
}

const mhe_module *mhe_load_module(const char *path, const mhe_host *host, char *error, size_t error_size)
{
    void *lib = open_library(path, error, error_size);
    if (! lib)
        return NULL;

    mhe_module_entry_fn entry;
    *(void **)&entry = find_symbol(lib, MHE_MODULE_ENTRY);
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

const mhe_fw_info *mhe_module_info(const char *path)
{
    char error[256];
    void *lib = open_library(path, error, sizeof(error));
    if (! lib)
        return NULL;

    mhe_fw_info_fn info;
    *(void **)&info = find_symbol(lib, MHE_FIRMWARE_INFO);
    const mhe_fw_info *fw = info ? info() : NULL;
    return fw && fw->size >= sizeof(mhe_fw_info) ? fw : NULL;
}

const mhe_fw_info *mhe_module_firmware(const mhe_module *module)
{
    if (! module || module->size < offsetof(mhe_module, info) + sizeof(module->info) || ! module->info)
        return NULL;
    return module->info->size >= sizeof(mhe_fw_info) ? module->info : NULL;
}

void mhe_default_state_dir(const char *base, const mhe_fw_info *info, char *dir, size_t dir_size)
{
    if (! info || ! info->tag[0])
    {
        snprintf(dir, dir_size, "%s", base);
        return;
    }

    /* git refuses tags starting with '.', so no ".." can come out of this */
    size_t n = (size_t)snprintf(dir, dir_size, "%s/", base);
    for (const char *p = info->tag; *p && n + 1 < dir_size; ++p)
        dir[n++] = isalnum((unsigned char)*p) || strchr("._-+", *p) ? *p : '_';
    if (n < dir_size)
        dir[n] = '\0';
}

static int make_dir(const char *dir)
{
#ifdef _WIN32
    return _mkdir(dir);
#else
    return mkdir(dir, 0755);
#endif
}

int mhe_mkdir_p(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s", dir);

    for (char *p = path + 1; *p; ++p)
    {
        if (*p != '/' && *p != '\\')
            continue;
        const char c = *p;
        *p = '\0';
        make_dir(path);
        *p = c;
    }
    return make_dir(path) == 0 || errno == EEXIST ? 0 : -1;
}
