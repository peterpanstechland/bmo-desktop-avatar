/**
 * @file bmo_ota.h
 * @brief Manual cloud OTA check for the Settings page.
 */

#ifndef __BMO_OTA_H__
#define __BMO_OTA_H__

#include <stdbool.h>

/**
 * Query silent upgrade info via matop.
 * Toasts: latest / found+download / offline / busy.
 * If an upgrade payload is returned, starts tuya_ota_start().
 */
void bmo_ota_check(void);

/** True while a check request is in flight or OTA download has been started. */
bool bmo_ota_is_busy(void);

#endif
