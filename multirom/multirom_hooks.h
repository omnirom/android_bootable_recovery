#ifndef MR_DEVICE_HOOKS_H
#define MR_DEVICE_HOOKS_H

#ifdef MR_DEVICE_RECOVERY_HOOKS

#if MR_DEVICE_RECOVERY_HOOKS >= 1
const char *mrom_hook_ubuntu_touch_get_extra_mounts(void);
#endif

#endif /* MR_DEVICE_RECOVERY_HOOKS */

#endif /* MR_DEVICE_HOOKS_H */