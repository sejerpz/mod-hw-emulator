/*
 * mod-hw-emulator — sys_host.c built for one device
 *
 * mod-system-control's sys_host.c knows the device at build time (_MOD_DEVICE_DWARF, _MOD_DEVICE_DUOX: pages,
 * subpages, actuators). The hosts load any device module, so they link one copy per device: this header,
 * given with -include and MHE_SYS_HOST_DEVICE=dwarf, renames its public functions (sys_host_setup becomes
 * mhe_sys_host_dwarf_setup...), and syscontrol.c calls the copy of the device the module declares.
 */

#ifndef MHE_SYS_HOST_DEVICE_H
#define MHE_SYS_HOST_DEVICE_H

#define MHE_SYS_HOST_CAT(device, name) mhe_sys_host_##device##_##name
#define MHE_SYS_HOST_NAME(device, name) MHE_SYS_HOST_CAT(device, name)
#define MHE_SH(name) MHE_SYS_HOST_NAME(MHE_SYS_HOST_DEVICE, name)

#define sys_host_setup                   MHE_SH(setup)
#define sys_host_process                 MHE_SH(process)
#define sys_host_destroy                 MHE_SH(destroy)
#define sys_host_get_compressor_mode     MHE_SH(get_compressor_mode)
#define sys_host_get_compressor_release  MHE_SH(get_compressor_release)
#define sys_host_get_noisegate_channel   MHE_SH(get_noisegate_channel)
#define sys_host_get_noisegate_decay     MHE_SH(get_noisegate_decay)
#define sys_host_get_noisegate_threshold MHE_SH(get_noisegate_threshold)
#define sys_host_get_pedalboard_gain     MHE_SH(get_pedalboard_gain)
#define sys_host_set_compressor_mode     MHE_SH(set_compressor_mode)
#define sys_host_set_compressor_release  MHE_SH(set_compressor_release)
#define sys_host_set_noisegate_channel   MHE_SH(set_noisegate_channel)
#define sys_host_set_noisegate_decay     MHE_SH(set_noisegate_decay)
#define sys_host_set_noisegate_threshold MHE_SH(set_noisegate_threshold)
#define sys_host_set_pedalboard_gain     MHE_SH(set_pedalboard_gain)
#define sys_host_set_hmi_page            MHE_SH(set_hmi_page)
#define sys_host_set_hmi_subpage         MHE_SH(set_hmi_subpage)

#endif
