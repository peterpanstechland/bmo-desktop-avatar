#ifndef __UI_POPUP_H__
#define __UI_POPUP_H__

/**
 * Transient popups drawn on the LVGL top layer, so they show up over any page.
 * All entries take the display lock themselves and are safe to call from the
 * button thread.
 */

/** Short message at the bottom of the screen that fades out on its own. */
void ui_popup_toast(const char *msg);

/** Show or hide the system info panel (IP, RSSI, free heap, firmware). */
void ui_popup_sysinfo_toggle(void);

/**
 * Centred message that stays until hidden, for feedback that has to track a
 * button being held. Calling it again just retitles the existing box.
 */
void ui_popup_hold_show(const char *msg);
void ui_popup_hold_hide(void);

#endif
