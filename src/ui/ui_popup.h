#ifndef __UI_POPUP_H__
#define __UI_POPUP_H__

/**
 * Transient popups drawn on the LVGL top layer, so they show up over any page.
 * All entries take the display lock themselves and are safe to call from the
 * button thread.
 */

/** Short centred message that fades out on its own. */
void ui_popup_toast(const char *msg);

/** Show or hide the system info panel (IP, RSSI, free heap, firmware). */
void ui_popup_sysinfo_toggle(void);

#endif
