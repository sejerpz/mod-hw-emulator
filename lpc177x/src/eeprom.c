/*
 * mod-hw-emulator — LPC177x EEPROM driver API (lpc177x_8x_eeprom.h), backed by a file
 *
 * Pages of 64 bytes. Addressing follows the NXP driver: byte address = page * 64 + offset, both masked to
 * 6 bits. A new unit reads all zeros, as the firmware expects (it then writes its defaults).
 */

#include "emu_core.h"

#include "LPC177x_8x.h"
#include "lpc177x_8x_eeprom.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* the driver masks the page number to 6 bits: room for 64 pages */
#define EEPROM_SIZE (EEPROM_PAGE_SIZE * 64)

static uint8_t g_data[EEPROM_SIZE];
static char g_path[1024];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_dirty;

void emu_eeprom_set_file(const char *path)
{
    snprintf(g_path, sizeof(g_path), "%s", path ? path : "");
}

/* writes are kept in memory and saved by emu_eeprom_flush(): a new unit writes its defaults one byte at a
 * time, and a file write each would slow the boot down (a lot on a network or WSL drive) */
static void save(void)
{
    g_dirty = 1;
}

void emu_eeprom_flush(void)
{
    pthread_mutex_lock(&g_mutex);
    if (! g_dirty || ! g_path[0])
    {
        pthread_mutex_unlock(&g_mutex);
        return;
    }
    g_dirty = 0;
    uint8_t copy[EEPROM_SIZE];
    memcpy(copy, g_data, sizeof(copy));
    pthread_mutex_unlock(&g_mutex);

    FILE *f = fopen(g_path, "wb");
    if (! f)
    {
        emu_log(MHE_LOG_WARNING, "eeprom: cannot write %s", g_path);
        return;
    }
    fwrite(copy, 1, sizeof(copy), f);
    fclose(f);
}

void EEPROM_Init(void)
{
    memset(g_data, 0, sizeof(g_data));
    if (! g_path[0])
        return;

    FILE *f = fopen(g_path, "rb");
    if (! f)
        return;
    if (fread(g_data, 1, sizeof(g_data), f) != sizeof(g_data))
        emu_log(MHE_LOG_WARNING, "eeprom: %s is short, the rest reads as zeros", g_path);
    fclose(f);
}

static size_t unit_size(EEPROM_Mode_Type mode)
{
    return mode == MODE_32_BIT ? 4u : mode == MODE_16_BIT ? 2u : 1u;
}

void EEPROM_Write(uint16_t page_offset, uint16_t page_address, void *data, EEPROM_Mode_Type mode, uint32_t size)
{
    const size_t start = (size_t)(page_address & 0x3f) * EEPROM_PAGE_SIZE + (size_t)(page_offset & 0x3f);
    size_t bytes = (size_t)size * unit_size(mode);
    if (start >= EEPROM_SIZE)
        return;
    if (start + bytes > EEPROM_SIZE)
        bytes = EEPROM_SIZE - start;

    pthread_mutex_lock(&g_mutex);
    memcpy(&g_data[start], data, bytes);
    save();
    pthread_mutex_unlock(&g_mutex);
}

void EEPROM_Read(uint16_t page_offset, uint16_t page_address, void *data, EEPROM_Mode_Type mode, uint32_t size)
{
    const size_t start = (size_t)(page_address & 0x3f) * EEPROM_PAGE_SIZE + (size_t)(page_offset & 0x3f);
    size_t bytes = (size_t)size * unit_size(mode);
    if (start >= EEPROM_SIZE)
    {
        memset(data, 0, bytes);
        return;
    }
    if (start + bytes > EEPROM_SIZE)
        bytes = EEPROM_SIZE - start;

    pthread_mutex_lock(&g_mutex);
    memcpy(data, &g_data[start], bytes);
    pthread_mutex_unlock(&g_mutex);
}

void EEPROM_Erase(uint16_t page_address)
{
    if (page_address >= EEPROM_PAGE_NUM)
        return;
    pthread_mutex_lock(&g_mutex);
    memset(&g_data[(size_t)page_address * EEPROM_PAGE_SIZE], 0, EEPROM_PAGE_SIZE);
    save();
    pthread_mutex_unlock(&g_mutex);
}

void EEPROM_PowerDown(FunctionalState NewState)
{
    (void)NewState;
}
