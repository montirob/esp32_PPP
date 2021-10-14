// Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <string.h>
#include "esp_log.h"
#include "esp_modem_dce_service.h"

/**
 * @brief Macro defined for error checking
 *
 */
static const char *DCE_TAG = "dce_service";
#define DCE_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                \
    {                                                                                 \
        if (!(a))                                                                     \
        {                                                                             \
            ESP_LOGE(DCE_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                            \
        }                                                                             \
    } while (0)

esp_err_t esp_modem_dce_handle_response_default( modem_dce_t * dce, const char * line )
{
   esp_err_t err = ESP_FAIL;
   if ( strstr( line, MODEM_RESULT_CODE_SUCCESS ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_SUCCESS );
      dce->baudStatus = MODEM_BRS_OK;
      ESP_LOGD(DCE_TAG, "SUCCESS");
   }
   else if ( strstr( line, MODEM_RESULT_CODE_ERROR ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_FAIL );
      dce->baudStatus = MODEM_BRS_OK;
      ESP_LOGD(DCE_TAG, "FAIL");
   }
   else
   {
      ESP_LOGW(DCE_TAG, "Unexpected modem response: %s", line);
   }
   return err;
}


esp_err_t esp_modem_dce_handle_at( modem_dce_t * dce, const char * line )
{
   esp_err_t err = ESP_FAIL;
   if ( strstr( line, MODEM_RESULT_CODE_SUCCESS ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_SUCCESS );
      ESP_LOGW(DCE_TAG, MODEM_RESULT_CODE_SUCCESS);
   }
   else if ( strstr( line, MODEM_RESULT_CODE_ERROR ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_FAIL );
      ESP_LOGW(DCE_TAG, MODEM_RESULT_CODE_ERROR);
   }
   else if ( strstr( line, "AT" ) )
   {
      ESP_LOGW(DCE_TAG, "AT" );
      err = ESP_OK;
   }
   else
   {
      ESP_LOGW(DCE_TAG, "DC %s", line);
   }
   return err;
}

esp_err_t esp_modem_dce_handle_ate( modem_dce_t * dce, const char * line )
{
   esp_err_t err = ESP_FAIL;

   if ( strstr( line, MODEM_RESULT_CODE_ATE0 ) )
   {
      /* an echo disable command with echo on respond the last time with the echo */
      err = ESP_OK;
   }
   else if ( strstr( line, MODEM_RESULT_CODE_ATE1 ) )
   {
      err = ESP_OK;
   }
   else if ( strstr( line, MODEM_RESULT_CODE_SUCCESS ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_SUCCESS );
   }
   else if ( strstr( line, MODEM_RESULT_CODE_ERROR ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_FAIL );
   }
   return err;
}
esp_err_t esp_modem_dce_sync(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = esp_modem_dce_handle_response_default;
    DCE_CHECK(dte->send_cmd(dte, "AT\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "sync failed", err);
    ESP_LOGD(DCE_TAG, "sync ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_dce_echo(modem_dce_t *dce, bool on)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = esp_modem_dce_handle_ate;
    if (on) {
        DCE_CHECK(dte->send_cmd(dte, "ATE1\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
        DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "enable echo failed", err);
        ESP_LOGD(DCE_TAG, "enable echo ok");
    } else {
        DCE_CHECK(dte->send_cmd(dte, "ATE0\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
        DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "disable echo failed", err);
        ESP_LOGD(DCE_TAG, "disable echo ok");
    }
    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_dce_factory_reset( modem_dce_t * dce )
{
   modem_dte_t *dte = dce->dte;
   dce->handle_line = esp_modem_dce_handle_response_default;

   DCE_CHECK( dte->send_cmd(dte, "AT&F\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err );
   DCE_CHECK( dce->state == MODEM_STATE_SUCCESS, "reset to factory failed", err );
   ESP_LOGI( DCE_TAG, "reset to factory ok" );

   return ESP_OK;
   err: return ESP_FAIL;
}

esp_err_t esp_modem_dce_store_profile(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = esp_modem_dce_handle_response_default;
    DCE_CHECK(dte->send_cmd(dte, "AT&W\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "save settings failed", err);
    ESP_LOGD(DCE_TAG, "save settings ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_dce_set_flow_ctrl(modem_dce_t *dce, modem_flow_ctrl_t flow_ctrl)
{
    modem_dte_t *dte = dce->dte;
    char command[16];
    int len = snprintf(command, sizeof(command), "AT+IFC=%d,%d\r", dte->flow_ctrl, flow_ctrl);
    DCE_CHECK(len < sizeof(command), "command too long: %s", err, command);
    dce->handle_line = esp_modem_dce_handle_response_default;
    DCE_CHECK(dte->send_cmd(dte, command, MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "set flow control failed", err);
    ESP_LOGD(DCE_TAG, "set flow control ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_dce_set_baud_rate(modem_dce_t *dce, uint32_t baudrate)
{
    modem_dte_t *dte = dce->dte;
    char command[16];
    int len = snprintf(command, sizeof(command), "AT+IPR=%d\r", baudrate);
    DCE_CHECK(len < sizeof(command), "command too long: %s", err, command);
    dce->handle_line = esp_modem_dce_handle_response_default;

    DCE_CHECK(dte->send_cmd(dte, command, MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "set_baud_rate failed", err);


    ESP_LOGD(DCE_TAG, "baudrate changed to: %d" , baudrate);
    return ESP_OK;
err:
    return ESP_FAIL;
}


esp_err_t esp_modem_dce_define_pdp_context(modem_dce_t *dce, uint32_t cid, const char *type, const char *apn)
{
    modem_dte_t *dte = dce->dte;
    char command[64];
    int len = snprintf(command, sizeof(command), "AT+CGDCONT=%d,\"%s\",\"%s\"\r", cid, type, apn);
    DCE_CHECK(len < sizeof(command), "command too long: %s", err, command);
    ESP_LOGD(DCE_TAG, " = %s", command );
    dce->handle_line = esp_modem_dce_handle_response_default;
    DCE_CHECK(dte->send_cmd(dte, command, MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "define pdp context failed", err);
    ESP_LOGD(DCE_TAG, "define pdp context ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_dce_hang_up(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = esp_modem_dce_handle_response_default;
    DCE_CHECK(dte->send_cmd(dte, "ATH\r", MODEM_COMMAND_TIMEOUT_HANG_UP) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "hang up failed", err);
    ESP_LOGD(DCE_TAG, "hang up ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_dce_answer(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = esp_modem_dce_handle_response_default;
    DCE_CHECK(dte->send_cmd(dte, "ATA\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "answer failed", err);
    ESP_LOGD(DCE_TAG, "answer ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t esp_modem_dce_set_auto_answer(modem_dce_t *dce, uint32_t ringNumber )
{
    modem_dte_t *dte = dce->dte;
    char command[16];
    int len = snprintf(command, sizeof(command), "ATS0=%d\r", ringNumber );
    DCE_CHECK(len < sizeof(command), "command too long: %s", err, command);
    dce->handle_line = esp_modem_dce_handle_response_default;
    DCE_CHECK(dte->send_cmd(dte, command, MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "set auto answer failed", err);
    ESP_LOGD(DCE_TAG, "set auto answer ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}
