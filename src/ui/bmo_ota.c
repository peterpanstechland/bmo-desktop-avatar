/**
 * @file bmo_ota.c
 * @brief Settings-page wrapper around matop silent upgrade query.
 */

#include "bmo_ota.h"
#include "ui_i18n.h"
#include "ui_popup.h"
#include "tal_api.h"
#include "tuya_iot.h"
#include "tuya_ota.h"
#include "matop_service.h"
#include "cJSON.h"

static volatile bool sg_check_pending = false;
static volatile bool sg_ota_active = false;

bool bmo_ota_is_busy(void)
{
    return sg_check_pending || sg_ota_active;
}

static void __upgrade_cb(atop_base_response_t *response, void *user_data)
{
    (void)user_data;

    sg_check_pending = false;

    if (!response || response->success == false) {
        ui_popup_toast(bmo_tr(BMO_STR_OTA_OFFLINE));
        return;
    }

    if (response->result == NULL) {
        ui_popup_toast(bmo_tr(BMO_STR_OTA_LATEST));
        return;
    }

    /* Same path as SDK auto-check: start download/flash. */
    if (OPRT_OK == tuya_ota_start(response->result)) {
        sg_ota_active = true;
        ui_popup_toast(bmo_tr(BMO_STR_OTA_FOUND));
        PR_NOTICE("[ota] manual check: upgrade started");
    } else {
        ui_popup_toast(bmo_tr(BMO_STR_OTA_BUSY));
        PR_WARN("[ota] tuya_ota_start failed");
    }
}

void bmo_ota_check(void)
{
    tuya_iot_client_t *client;
    int rt;

    if (bmo_ota_is_busy()) {
        ui_popup_toast(bmo_tr(BMO_STR_OTA_BUSY));
        return;
    }

    client = tuya_iot_client_get();
    if (!client || !client->is_activated || !tuya_mqtt_connected(&client->mqctx)) {
        ui_popup_toast(bmo_tr(BMO_STR_OTA_OFFLINE));
        return;
    }

    sg_check_pending = true;
    ui_popup_toast(bmo_tr(BMO_STR_OTA_CHECKING));

    rt = matop_service_auto_upgrade_info_get(&client->matop, __upgrade_cb, client);
    if (rt != OPRT_OK) {
        sg_check_pending = false;
        ui_popup_toast(bmo_tr(BMO_STR_OTA_OFFLINE));
        PR_ERR("[ota] auto_upgrade_info_get rt=%d", rt);
    }
}
