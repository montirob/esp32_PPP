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
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "ec21.h"
#include "freertos/FreeRTOS.h"
#include "DrvNvs.h"

#define MODEM_RESULT_CODE_POWERDOWN "POWERED DOWN"

/* if 1 enable th emodule automatic answer: used for laboratory tests*/
#define AUTO_ANSWER_CMC_TEST         0

/* set the baudrate to configure the LTE modem after the first boot*/
#define EC21_WORKING_BAUDRATE       921600

#define BAND1_LTE_MASK                  0x1
#define BAND3_LTE_MASK                  0x4
#define BAND5_LTE_MASK                  0x10
#define BAND7_LTE_MASK                  0x40
#define BAND8_LTE_MASK                  0x80

#define ENABLE_FAST_SHUTDOWN_MAX_RETRY          10

/**
 * @brief Macro defined for error checking
 *
 */
static const char *DCE_TAG = "ec21";
#define DCE_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                \
    {                                                                                 \
        if (!(a))                                                                     \
        {                                                                             \
            ESP_LOGE(DCE_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                            \
        }                                                                             \
    } while (0)

/**
 * @brief EC21 Modem
 *
 */
typedef struct {
    void *priv_resource; /*!< Private resource */
    modem_dce_t parent;  /*!< DCE parent class */
} ec21_modem_dce_t;


static ec21_modem_dce_t *ec21_dce = (void*)0;

static DrvNvs_element_t *gpDrvNvs_baudrate = (void*)0;


/**
 * @brief Handle response from AT+CSQ
 */
static esp_err_t ec21_handle_csq(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    ec21_modem_dce_t *ec21_dce = __containerof(dce, ec21_modem_dce_t, parent);
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (!strncmp(line, "+CSQ", strlen("+CSQ"))) {
        /* store value of rssi and ber */
        uint32_t **csq = ec21_dce->priv_resource;
        /* +CSQ: <rssi>,<ber> */
        sscanf(line, "%*s%d,%d", csq[0], csq[1]);
        err = ESP_OK;
    }
    return err;
}

/**
 * @brief Handle response from AT+CBC
 */
static esp_err_t ec21_handle_cbc(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    ec21_modem_dce_t *ec21_dce = __containerof(dce, ec21_modem_dce_t, parent);
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (!strncmp(line, "+CBC", strlen("+CBC"))) {
        /* store value of bcs, bcl, voltage */
        uint32_t **cbc = ec21_dce->priv_resource;
        /* +CBC: <bcs>,<bcl>,<voltage> */
        sscanf(line, "%*s%d,%d,%d", cbc[0], cbc[1], cbc[2]);
        err = ESP_OK;
    }
    return err;
}

/**
 * @brief Handle response from +++
 */
static esp_err_t ec21_handle_exit_data_mode(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_NO_CARRIER)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    }
    return err;
}

/**
 * @brief Handle response from ATD*99#
 */
static esp_err_t ec21_handle_atd_ppp( modem_dce_t * dce, const char * line )
{
   esp_err_t err = ESP_FAIL;
   if ( strstr( line, MODEM_RESULT_CODE_CONNECT ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_SUCCESS );
   }
   else if ( strstr( line, MODEM_RESULT_CODE_ERROR ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_FAIL );
   }
   else if ( strstr( line, MODEM_RESULT_CODE_NO_CARRIER ) )
   {
      err = esp_modem_process_command_done( dce, MODEM_STATE_FAIL );
   }
   return err;
}

/**
 * @brief Handle response from AT+CGMM
 */
static esp_err_t ec21_handle_cgmm(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        int len = snprintf(dce->name, MODEM_MAX_NAME_LENGTH, "%s", line);
        if (len > 2) {
            /* Strip "\r\n" */
            strip_cr_lf_tail(dce->name, len);
            err = ESP_OK;
        }
    }
    return err;
}

/**
 * @brief Handle response from AT+CGSN
 */
static esp_err_t ec21_handle_cgsn(modem_dce_t *dce, const char *line)
{
   esp_err_t err = ESP_FAIL;
   if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
      err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
   } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
      err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
   } else {
      int len = snprintf(dce->imei, MODEM_IMEI_LENGTH + 1, "%s", line);
      if (len > 2) {
         /* Strip "\r\n" */
         strip_cr_lf_tail(dce->imei, len);
         err = ESP_OK;
      }
   }
   return err;
}

/**
 * @brief Handle response from AT+CIMI
 */
static esp_err_t ec21_handle_cimi(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;

    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        int len = snprintf(dce->imsi, MODEM_IMSI_LENGTH + 1, "%s", line);
        if (len > 2) {
            /* Strip "\r\n" */
            strip_cr_lf_tail(dce->imsi, len);
            err = ESP_OK;
        }
    }
    return err;
}


static esp_err_t ec21_handle_default(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;

    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
        /* set the baudrate ok*/
        dce->baudStatus = MODEM_BRS_OK;
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        int len = snprintf(dce->imsi, MODEM_IMSI_LENGTH + 1, "%s", line);
        if (len > 2) {
            /* Strip "\r\n" */
            strip_cr_lf_tail(dce->imsi, len);
            err = ESP_OK;
        }
    }
    return err;
}

static esp_err_t ec21_handle_QCFG(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    uint32_t bandval = 0;
    uint32_t ltebandval = 0;
    uint32_t tdsbandval = 0;

    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
       err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (!strncmp(line, "+QCFG", strlen("+QCFG"))) {
       /* +QCFG: �band�,<bandval>,<ltebandval>,<tdsbandval> */
       if (strstr(line, "band"))
       {

          char *ptr;
          ptr = strchr( line, ',' );
          ptr++;

          sscanf(ptr, "%x,%x,%x", &bandval, &ltebandval, &tdsbandval);

          ESP_LOGD(DCE_TAG,"bandval = %x,    ltebandval = %x, tdsbandval = %x\n" , bandval , ltebandval, tdsbandval);

          if ( (ltebandval & BAND7_LTE_MASK) != 0 )
          {
             *(bool*)ec21_dce->priv_resource = true;
             printf("B7 IS ON\n");
          }
          else
          {
             *(bool*)ec21_dce->priv_resource = false;
             printf("B7 IS OFF\n");
          }

          err = ESP_OK;
       }
       ESP_LOGD(DCE_TAG,"%s",line);

    }
    return err;
}

static esp_err_t ec21_handle_QNWINFO(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;

    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
       err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (!strncmp(line, "+QNWINFO", strlen("+QNWINFO"))) {
       ESP_LOGI(DCE_TAG,"%s",line);
       err = ESP_OK;
    }
    return err;
}

static esp_err_t ec21_handle_CREG(modem_dce_t *dce, const char *line )
{
   esp_err_t err = ESP_FAIL;
   ec21_modem_dce_t *ec21_dce = __containerof(dce, ec21_modem_dce_t, parent);
   if (strstr(line, MODEM_RESULT_CODE_SUCCESS))
   {
      err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
   }
   else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
      err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
   }
   else if (!strncmp(line, "+CREG", strlen("+CREG")))
   {
      int32_t n = 0;
      modem_network_status_t * pStat = (modem_network_status_t *)ec21_dce->priv_resource;
      sscanf(line, "%*s%d,%d", &n, (uint32_t*)pStat);
      //printf("CREG resp: %d,%d\n", n, *pStat);
      err = ESP_OK;
   }

   return err;
}


static esp_err_t ec21_handle_CPIN(modem_dce_t *dce, const char *line )
{
   esp_err_t err = ESP_FAIL;

   if (strstr(line, MODEM_RESULT_CODE_SUCCESS))
   {
      err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
   }
   else if ( !strncmp( line, "+CME ERROR", strlen("+CME ERROR") ) )
   {
      char *ptr;
      char * pEnd;
      uint8_t zErrorCode = 0;

      ptr = strchr( line, ':' );
      ptr++;
      zErrorCode = strtol(ptr,&pEnd,10);
      ESP_LOGI(__func__, "CME ERROR = %d", zErrorCode);

      if ( 10 == zErrorCode )
      {
         dce->simStatus = MODEM_SIM_NOT_INSERTED;
      }
      else
      {
         dce->simStatus = MODEM_SIM_UNKNOWN;
      }

      err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
   }
   else if (strstr(line, MODEM_RESULT_CODE_ERROR))
   {
      err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
   }
   else if ( !strncmp( line, "+CPIN", strlen("+CPIN") ) )
   {
      char *ptr;
      ptr = strchr( line, ':' );
      ptr++;
      if (strstr(ptr, "READY"))
      {
         dce->simStatus = MODEM_SIM_READY;
      }
      else if (strstr(ptr, "SIM PIN"))
      {
         dce->simStatus = MODEM_SIM_PIN;
      }
      else if (strstr(ptr, "SIM PUK"))
      {
         dce->simStatus = MODEM_SIM_PUK;
      }
      else if (strstr(ptr, "NOT INSERTED"))
      {
         dce->simStatus = MODEM_SIM_NOT_INSERTED;
      }
      else
      {
         dce->simStatus = MODEM_SIM_UNKNOWN;
      }

      err = ESP_OK;
   }

   return err;
}


/**
 * @brief Handle response from AT+COPS?
 */
static esp_err_t ec21_handle_cops(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;

    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (!strncmp(line, "+COPS", strlen("+COPS"))) {
        /* there might be some random spaces in operator's name, we can not use sscanf to parse the result */
        /* strtok will break the string, we need to create a copy */
        size_t len = strlen(line);
        char *line_copy = malloc(len + 1);
        strcpy(line_copy, line);
        /* +COPS: <mode>[, <format>[, <oper>]] */
        char *str_ptr = NULL;
        char *p[3];
        uint8_t i = 0;
        /* strtok will broke string by replacing delimiter with '\0' */
        p[i] = strtok_r(line_copy, ",", &str_ptr);
        while (p[i]) {
            p[++i] = strtok_r(NULL, ",", &str_ptr);
        }
        if (i >= 3)
        {
            int len = snprintf(dce->oper, MODEM_MAX_OPERATOR_LENGTH, "%s", p[2]);
            if (len > 2) {
                /* Strip "\r\n" */
                strip_cr_lf_tail(dce->oper, len);
                err = ESP_OK;
            }
        }
        else if (i == 1)
        {
           /*COPS:0 operator still not selected; handle is ok but check the operator len in order to know if the operator is selected*/
           err = ESP_OK;
        }
        free(line_copy);
    }

    return err;
}

/**
 * @brief Handler of the starting unrequested strings
 */
static esp_err_t ec21_starting_up_handler(modem_dce_t *dce, const char *line)
{
   esp_err_t err = ESP_FAIL;

   if (strstr(line, "RDY"))
   {
      err = ESP_OK;
      ESP_LOGI(__func__, "module is ready");
      dce->baudStatus = MODEM_BRS_OK;
   }
   else if (strstr(line, "+CPIN"))
   {
      err = ec21_handle_CPIN(dce, line );
      dce->baudStatus = MODEM_BRS_OK;
   }
   else if (!strncmp(line, "+QUSIM", strlen("+QUSIM")))
   {
      /* QUSIM: 1 == Use USIM card, 0 == use SIM card*/
      /* +QUSIM: <type> */
      err = ESP_OK;
      char *ptr;
      char * pEnd;
      uint8_t zNumber = 0;
      ptr = strchr( line, ':' );
      ptr++;
      zNumber = strtol(ptr,&pEnd,10);
      ESP_LOGI(__func__, "QUSIM type = %d (0 == SIM, 1 == USIM)"  , zNumber);
      dce->baudStatus = MODEM_BRS_OK;
   }
   else if (!strncmp(line, "+CFUN", strlen("+CFUN")))
   {
      /* CFUN: 0 == min funct, 1 == full funct*/
      /* +CFUN: <fun> */
      err = ESP_OK;
      char *ptr;
      char * pEnd;
      uint8_t zNumber = 0;
      ptr = strchr( line, ':' );
      ptr++;
      zNumber = strtol(ptr,&pEnd,10);
      ESP_LOGI(__func__, "CFUN = %d (0 == min funct, 1 == full funct)"  , zNumber);
      dce->baudStatus = MODEM_BRS_OK;
   }
   /* used strstr because sometime spaces or new lines arrives before +QIND */
   else if (strstr(line, "+QIND"))
   {
      err = ESP_OK;  //SMS DONE   or PB DONE
      dce->baudStatus = MODEM_BRS_OK;
   }
   else
   {
      ESP_LOGI(__func__, "%s", line);
   }

   return err;
}

/**
 * @brief Handle response from AT+QPOWD=X
 */
static esp_err_t ec21_handle_power_down(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;

    printf (line);
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = ESP_OK;
    } else if (strstr(line, MODEM_RESULT_CODE_POWERDOWN)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    }
    return err;
}

/**
 * @brief Get signal quality
 *
 * @param dce Modem DCE object
 * @param rssi received signal strength indication
 * @param ber bit error ratio
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_get_signal_quality(modem_dce_t *dce, uint32_t *rssi, uint32_t *ber)
{
    modem_dte_t *dte = dce->dte;
    ec21_modem_dce_t *ec21_dce = __containerof(dce, ec21_modem_dce_t, parent);
    uint32_t *resource[2] = {rssi, ber};
    ec21_dce->priv_resource = resource;
    dce->handle_line = ec21_handle_csq;
    DCE_CHECK(dte->send_cmd(dte, "AT+CSQ\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "inquire signal quality failed", err);
    ESP_LOGD(DCE_TAG, "inquire signal quality ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Get battery status
 *
 * @param dce Modem DCE object
 * @param bcs Battery charge status
 * @param bcl Battery connection level
 * @param voltage Battery voltage
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_get_battery_status(modem_dce_t *dce, uint32_t *bcs, uint32_t *bcl, uint32_t *voltage)
{
    modem_dte_t *dte = dce->dte;
    ec21_modem_dce_t *ec21_dce = __containerof(dce, ec21_modem_dce_t, parent);
    uint32_t *resource[3] = {bcs, bcl, voltage};
    ec21_dce->priv_resource = resource;
    dce->handle_line = ec21_handle_cbc;
    DCE_CHECK(dte->send_cmd(dte, "AT+CBC\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "inquire battery status failed", err);
    ESP_LOGD(DCE_TAG, "inquire battery status ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Get SIM presence
 *
 * @param dce Modem DCE object
 * @param bcs Battery charge status
 * @param bcl Battery connection level
 * @param voltage Battery voltage
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_get_sim_status(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = ec21_handle_CPIN;
    DCE_CHECK(dte->send_cmd(dte, "AT+CPIN?\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "inquire SIM status failed", err);
    ESP_LOGD(DCE_TAG, "inquire SIM status ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Set Working Mode
 *
 * @param dce Modem DCE object
 * @param mode woking mode
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_set_working_mode(modem_dce_t *dce, modem_mode_t mode)
{
    modem_dte_t *dte = dce->dte;
    switch (mode) {
    case MODEM_COMMAND_MODE:
        dce->handle_line = ec21_handle_exit_data_mode;
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (dte->send_cmd(dte, "+++", MODEM_COMMAND_TIMEOUT_MODE_CHANGE) != ESP_OK) {
            // "+++" Could fail if we are already in the command mode.
            // in that case we ignore the timeout and re-sync the modem
            ESP_LOGE(DCE_TAG, "Sending \"+++\" command failed");
            dce->handle_line = esp_modem_dce_handle_response_default;
            DCE_CHECK(dte->send_cmd(dte, "AT\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
            DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "sync failed", err);
        } else {
            DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "enter command mode failed", err);
        }
        ESP_LOGI(DCE_TAG, "enter command mode ok");
        dce->mode = MODEM_COMMAND_MODE;
        break;
    case MODEM_PPP_MODE:
        dce->handle_line = ec21_handle_atd_ppp;
        DCE_CHECK(dte->send_cmd(dte, "ATD*99***1#\r", MODEM_COMMAND_TIMEOUT_MODE_CHANGE) == ESP_OK, "send command failed", err);
        if (dce->state != MODEM_STATE_SUCCESS) {
            // Initiate PPP mode could fail, if we've already "dialed" the data call before.
            // in that case we retry with "ATO" to just resume the data mode
            ESP_LOGD(DCE_TAG, "enter ppp mode failed, retry with ATO");
            dce->handle_line = ec21_handle_atd_ppp;
            DCE_CHECK(dte->send_cmd(dte, "ATO\r", MODEM_COMMAND_TIMEOUT_MODE_CHANGE) == ESP_OK, "send command failed", err);
            DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "enter ppp mode failed", err);
        }
        ESP_LOGD(DCE_TAG, "enter ppp mode ok");
        dce->mode = MODEM_PPP_MODE;
        break;
    default:
        ESP_LOGW(DCE_TAG, "unsupported working mode: %d", mode);
        goto err;
        break;

    }
    return ESP_OK;
err:
    //dte->send_data_lock = false;
    return ESP_FAIL;
}


/*
   * @param mode woking mode
   * @return esp_err_t
   *      - ESP_OK on success
   *      - ESP_FAIL on error
 */

static esp_err_t ec21_set_urc_port( ec21_modem_dce_t *ec21_dce )
{
   modem_dte_t *dte = ec21_dce->parent.dte;
   ec21_dce->parent.handle_line = ec21_handle_default;

   DCE_CHECK( dte->send_cmd(dte, "AT+QURCCFG=\"urcport\",\"uart1\"\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err );
   ESP_LOGD( DCE_TAG, "Set urc port ok" );

   return ESP_OK;
   err: return ESP_FAIL;
}


/*
   * @param mode woking mode
   * @return esp_err_t
   *      - ESP_OK on success
   *      - ESP_FAIL on error
 */

static esp_err_t ec21_set_dtr_mode( ec21_modem_dce_t *ec21_dce, ec21_dtrMode_t dtrMode  )
{
   modem_dte_t *dte = ec21_dce->parent.dte;
   char command[16];
   int len = snprintf(command, sizeof(command), "AT&D%d\r", dtrMode);
   DCE_CHECK(len < sizeof(command), "command too long: %s", err, command);
   ec21_dce->parent.handle_line = ec21_handle_default;
   DCE_CHECK( dte->send_cmd(dte, command, MODEM_COMMAND_TIMEOUT_DEFAULT ) == ESP_OK, "send command failed", err );
   ESP_LOGD( DCE_TAG, "Set DTR mode ok" );

   return ESP_OK;
   err: return ESP_FAIL;
}

/**
 * @brief Set Working Mode
 *
 * @param dce Modem DCE object
 * @param mode woking mode
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_enable_fast_shutdown( ec21_modem_dce_t *ec21_dce )
{
    modem_dte_t *dte = ec21_dce->parent.dte;
    ec21_dce->parent.handle_line = ec21_handle_default;

    DCE_CHECK(dte->send_cmd(dte, "AT+QCFG=\"fast/poweroff\",1\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "enable_fast_shutdown failed", err);
    ESP_LOGI(DCE_TAG, "Set fast poweroff ok");

    return ESP_OK;
err:
    return ESP_FAIL;
}

static esp_err_t ec21_get_fast_shutdown_state( ec21_modem_dce_t *ec21_dce )
{
    modem_dte_t *dte = ec21_dce->parent.dte;
    ec21_dce->parent.handle_line = ec21_handle_default;

    DCE_CHECK(dte->send_cmd(dte, "AT+QCFG=\"fast/poweroff\"\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "ec21_get_fast_shutdown_state failed", err);
    ESP_LOGD(DCE_TAG, "Set fast poweroff ok");

    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Power down
 *
 * @param ec21_dce ec21 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_power_down(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = ec21_handle_power_down;
    DCE_CHECK(dte->send_cmd(dte, "AT+QPOWD=1\r", MODEM_COMMAND_TIMEOUT_POWEROFF) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "power down failed", err);
    ESP_LOGD(DCE_TAG, "power down ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Power down fast for emergency (supply fail)
 *
 * @param ec21_dce ec21 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_power_down_fast(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = ec21_handle_power_down;
    DCE_CHECK(dte->send_cmd(dte, "AT+QPOWD=0\r", MODEM_COMMAND_TIMEOUT_FAST_POWEROFF) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "fast power down failed", err);
    ESP_LOGD(DCE_TAG, "power down ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}


/**
 * @brief Get DCE module name
 *
 * @param ec21_dce ec21 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_get_module_name(ec21_modem_dce_t *ec21_dce)
{
    modem_dte_t *dte = ec21_dce->parent.dte;
    ec21_dce->parent.handle_line = ec21_handle_cgmm;
    DCE_CHECK(dte->send_cmd(dte, "AT+CGMM\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "get module name failed", err);
    ESP_LOGD(DCE_TAG, "get module name ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}


static esp_err_t enable_roaming(ec21_modem_dce_t *ec21_dce)
{
    modem_dte_t *dte = ec21_dce->parent.dte;
    ec21_dce->parent.handle_line = ec21_handle_default;
    /* using AUTo configuration for enabled setting: seems work better with some operators*/
    DCE_CHECK(dte->send_cmd(dte, "AT+QCFG=\"roamservice\",255,1\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "roaming enable failed", err);
    ESP_LOGI(DCE_TAG, "roaming enabled");
    return ESP_OK;
err:
    return ESP_FAIL;
}

static esp_err_t disable_roaming(ec21_modem_dce_t *ec21_dce)
{
    modem_dte_t *dte = ec21_dce->parent.dte;
    ec21_dce->parent.handle_line = ec21_handle_default;
    DCE_CHECK(dte->send_cmd(dte, "AT+QCFG=\"roamservice\",1,1\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "roaming disable failed", err);
    ESP_LOGI(DCE_TAG, "roaming disabled");
    return ESP_OK;
err:
    return ESP_FAIL;
}

static esp_err_t get_band7_configuration(ec21_modem_dce_t *ec21_dce, bool *isEnabled)
{
   modem_dte_t *dte = ec21_dce->parent.dte;
   ec21_dce->parent.handle_line = ec21_handle_QCFG;
   ec21_dce->priv_resource = isEnabled;

   DCE_CHECK(dte->send_cmd(dte, "AT+QCFG=\"band\"\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
   DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "get band state failed", err);

   return ESP_OK;
err:
   return ESP_FAIL;
}

static esp_err_t get_network_status(modem_dce_t *dce, modem_network_status_t *status)
{
    modem_dte_t *dte = dce->dte;
    ec21_modem_dce_t *ec21_dce = __containerof(dce, ec21_modem_dce_t, parent);
    ec21_dce->priv_resource = status;
    ec21_dce->parent.handle_line = ec21_handle_CREG;
    DCE_CHECK(dte->send_cmd(dte, "AT+CREG?\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "read network state failed", err);
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 *
 *
 * \brief
 *
 * \param ec21_dce
 * \return
 */
/**
 * @brief Get DCE module IMEI number
 *
 * @param ec21_dce ec21 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_get_imei_number(ec21_modem_dce_t *ec21_dce)
{
    modem_dte_t *dte = ec21_dce->parent.dte;
    ec21_dce->parent.handle_line = ec21_handle_cgsn;
    DCE_CHECK(dte->send_cmd(dte, "AT+CGSN\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "get imei number failed", err);
    ESP_LOGD(DCE_TAG, "get imei number ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Get DCE module IMSI number
 *
 * @param ec21_dce ec21 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_get_imsi_number(ec21_modem_dce_t *ec21_dce)
{
    modem_dte_t *dte = ec21_dce->parent.dte;
    ec21_dce->parent.handle_line = ec21_handle_cimi;
    DCE_CHECK(dte->send_cmd(dte, "AT+CIMI\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "get imsi number failed", err);
    ESP_LOGD(DCE_TAG, "get imsi number ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Get Operator's name
 *
 * @param ec21_dce ec21 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t ec21_get_operator_name(ec21_modem_dce_t *ec21_dce)
{
    modem_dte_t *dte = ec21_dce->parent.dte;
    ec21_dce->parent.handle_line = ec21_handle_cops;
    ec21_dce->parent.state = MODEM_STATE_FAIL;
    DCE_CHECK(dte->send_cmd(dte, "AT+COPS?\r", MODEM_COMMAND_TIMEOUT_OPERATOR) == ESP_OK, "send command failed", err);
    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "get network operator failed", err);
    return ESP_OK;
err:
    return ESP_FAIL;
}

/* command not managed because the response is longer than the uart HW FIFO buffer*/
//static esp_err_t ec21_get_op_list(ec21_modem_dce_t *ec21_dce)
//{
//    modem_dte_t *dte = ec21_dce->parent.dte;
//    ec21_dce->parent.handle_line = ec21_handle_cops;
//    DCE_CHECK(dte->send_cmd(dte, "AT+COPS=?\r", MODEM_COMMAND_TIMEOUT_OPERATOR) == ESP_OK, "send command failed", err);
//    DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "Get list of operator failed", err);
//    ESP_LOGI(DCE_TAG, "get op list operator ok");
//    return ESP_OK;
//err:
//    return ESP_FAIL;
//}

/**
 * @brief Deinitialize EC21 object
 *
 * @param dce Modem DCE object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on fail
 */
static esp_err_t ec21_deinit(modem_dce_t *dce)
{
    ec21_modem_dce_t *ec21_dce = __containerof(dce, ec21_modem_dce_t, parent);
    if (dce->dte) {
        dce->dte->dce = NULL;
    }
    free(ec21_dce);
    return ESP_OK;
}

modem_dce_t *ec21_init(modem_dte_t *dte)
{
    DCE_CHECK(dte, "DCE should bind with a DTE", err);
    /* malloc memory for ec21_dce object */
    ec21_dce = calloc(1, sizeof(ec21_modem_dce_t));
    DCE_CHECK(ec21_dce, "calloc ec21_dce failed", err);
    /* Bind DTE with DCE */
    ec21_dce->parent.dte = dte;
    dte->dce = &(ec21_dce->parent);
    /* Bind methods */
    ec21_dce->parent.handle_line = ec21_starting_up_handler;
    ec21_dce->parent.sync = esp_modem_dce_sync;
    ec21_dce->parent.echo_mode = esp_modem_dce_echo;
    ec21_dce->parent.store_profile = esp_modem_dce_store_profile;
    ec21_dce->parent.set_flow_ctrl = esp_modem_dce_set_flow_ctrl;
    ec21_dce->parent.define_pdp_context = esp_modem_dce_define_pdp_context;
    ec21_dce->parent.hang_up = esp_modem_dce_hang_up;
    ec21_dce->parent.answer = esp_modem_dce_answer;
    ec21_dce->parent.get_signal_quality = ec21_get_signal_quality;
    ec21_dce->parent.get_network_status = get_network_status;
    ec21_dce->parent.get_battery_status = ec21_get_battery_status;
    ec21_dce->parent.set_working_mode = ec21_set_working_mode;
    ec21_dce->parent.power_down = ec21_power_down;
    ec21_dce->parent.fast_power_down = ec21_power_down_fast;
    ec21_dce->parent.deinit = ec21_deinit;
    ec21_dce->parent.baudStatus = MODEM_BRS_UNKNOWN;

    gpDrvNvs_baudrate = DrvNvs_GetElement( DRVNVS_FACTORY_PARAMS_ID, DRVNVS_F_LTE_BAUDRATE_ID );

    return &(ec21_dce->parent);
err:
    return NULL;
}



esp_err_t ec21_configure( modem_dce_t * dce )
{
   uint32_t enableFastShutdownretry = 0;

   /* Sync between DTE and DCE */
   for (uint32_t sincretry = 0; sincretry < 3; sincretry++)
   {
      ESP_LOGI( DCE_TAG, "esp_modem_dce_sync" );
      if ( esp_modem_dce_sync(dce) == ESP_OK )
      {
         break;
      }
      vTaskDelay( pdMS_TO_TICKS( 500 ) );
   }
   vTaskDelay( pdMS_TO_TICKS( 300 ) );
   ESP_LOGI( DCE_TAG, "esp_modem_dce_factory_reset" );
   DCE_CHECK( esp_modem_dce_factory_reset(dce) == ESP_OK, "factory reset failed", err_io );

   vTaskDelay( pdMS_TO_TICKS( 300 ) );

   /* Close echo */
   ESP_LOGI( DCE_TAG, "esp_modem_dce_echo" );
   DCE_CHECK( esp_modem_dce_echo(dce, false) == ESP_OK, "close echo mode failed", err_io );


   static const uint32_t baudrate = EC21_WORKING_BAUDRATE;

   /* the LTE module is still the default 115200, change it! */
   if ( baudrate != *(uint32_t*)gpDrvNvs_baudrate->handler )
   {
      ESP_LOGI( DCE_TAG, "SETBAUDRATE: %d, in NVS found: %d", baudrate, *(uint32_t*)gpDrvNvs_baudrate->handler );
      DCE_CHECK( esp_modem_dce_set_baud_rate(dce, baudrate) == ESP_OK, "set DCE baud rate failed", err_io );

      vTaskDelay( pdMS_TO_TICKS( 300 ) );

      DCE_CHECK( dce->dte->change_dte_baudrate( dce->dte, baudrate )== ESP_OK, "set DTE baud rate failed", err_io );

      vTaskDelay( pdMS_TO_TICKS( 300 ) );

      DCE_CHECK( esp_modem_dce_store_profile(dce) == ESP_OK, "store profile failed", err_io );

      DrvNvs_SetElement( DRVNVS_FACTORY_PARAMS_ID, DRVNVS_F_LTE_BAUDRATE_ID, &baudrate );
   }

   DCE_CHECK( ec21_dce, "ec21_dce not intialized", err_io );

   /*get sim status */
   vTaskDelay( pdMS_TO_TICKS( 300 ) );
   ESP_LOGI( DCE_TAG, "ec21_get_sim_status" );
   DCE_CHECK( ec21_get_sim_status(dce) == ESP_OK, "get SIM status failed", err_io );

   /* set urc port */
  // vTaskDelay( pdMS_TO_TICKS( 300 ) );
  // ESP_LOGI( DCE_TAG, "ec21_set_urc_port" );
  // DCE_CHECK( ec21_set_urc_port(ec21_dce) == ESP_OK, "set URC port failed", err_io );

#if AUTO_ANSWER_CMC_TEST == 1
   vTaskDelay( pdMS_TO_TICKS( 300 ) );
   ESP_LOGI( DCE_TAG, "ec21_setup_auto_answer after 1 ring" );
   DCE_CHECK( esp_modem_dce_set_auto_answer(dce, 1) == ESP_OK, "set up auto answer failed", err_io );
#endif

   /* set DTR mode */
   vTaskDelay( pdMS_TO_TICKS( 300 ) );
   ESP_LOGI( DCE_TAG, "ec21_set_dtr_mode" );
   DCE_CHECK( ec21_set_dtr_mode(ec21_dce, EC21_DTR_CMD_MODE_AND_DISCONNECT) == ESP_OK, "set DTR behavior failed", err_io );

   /* Enable fast shutdown */
   vTaskDelay( pdMS_TO_TICKS( 1000 ) );

   ESP_LOGI( DCE_TAG, "ec21_enable_fast_shutdown" );
   /* perform some retries because if this command is sent to early after a modem reboot fails with ERROR*/
   while ( ( ec21_enable_fast_shutdown(ec21_dce) != ESP_OK ) &&
           ( enableFastShutdownretry <= ENABLE_FAST_SHUTDOWN_MAX_RETRY ) )
   {
      enableFastShutdownretry++;
      ESP_LOGW(DCE_TAG, "retry n: %d of 10", enableFastShutdownretry);
      vTaskDelay( pdMS_TO_TICKS( 1000 ) );
   }

   if ( enableFastShutdownretry >= ENABLE_FAST_SHUTDOWN_MAX_RETRY )
   {
      ESP_LOGW(DCE_TAG, "Failed to enable fast shutdown");
   }

   return ESP_OK;
err_io:

   return ESP_FAIL;
}


esp_err_t ec21_get_module_info( modem_dce_t * dce )
{
   DCE_CHECK( ec21_dce, "ec21_dce not intialized", err_io );

   /* Get Module name */
   ESP_LOGD( DCE_TAG, "ec21_get_module_name" );
   DCE_CHECK( ec21_get_module_name(ec21_dce) == ESP_OK, "get module name failed", err_io );
   /* Get IMEI number */
   ESP_LOGD( DCE_TAG, "ec21_get_imei_number" );
   DCE_CHECK( ec21_get_imei_number(ec21_dce) == ESP_OK, "get imei failed", err_io );
   /* Get IMSI number */
   ESP_LOGD( DCE_TAG, "ec21_get_imsi_number" );
   DCE_CHECK( ec21_get_imsi_number(ec21_dce) == ESP_OK, "get imsi failed", err_io );
   /* Get operator name */
   ESP_LOGD( DCE_TAG, "ec21_get_operator_name" );
   DCE_CHECK( ec21_get_operator_name(ec21_dce) == ESP_OK, "get operator name failed", err_io );

   return ESP_OK;
err_io:

   return ESP_FAIL;
}


esp_err_t ec21_enable_roaming( modem_dce_t * dce, bool isEnabled )
{
   DCE_CHECK( ec21_dce, "ec21_dce not intialized", err_io );

   if ( isEnabled )
   {
     DCE_CHECK( enable_roaming(ec21_dce) == ESP_OK, "enable roaming failed", err_io );
   }
   else
   {
     DCE_CHECK( disable_roaming(ec21_dce) == ESP_OK, "disable roaming failed", err_io );
   }

   return ESP_OK;
err_io:

   return ESP_FAIL;
}

esp_err_t ec21_get_band7_state( modem_dce_t * dce, bool *isEnabled )
{
   DCE_CHECK( ec21_dce, "ec21_dce not intialized", err_io );

   DCE_CHECK( get_band7_configuration(ec21_dce, isEnabled) == ESP_OK, "get band state failed", err_io );

   return ESP_OK;
   err_io:

   return ESP_FAIL;
}

esp_err_t ec21_set_band7_state(modem_dce_t *dce, bool enable )
{
   modem_dte_t *dte = ec21_dce->parent.dte;
   ec21_dce->parent.handle_line = ec21_handle_QCFG;

   if ( true == enable)
   {
      DCE_CHECK(dte->send_cmd(dte, "AT+QCFG=\"band\",0,800d5,0,1\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
      DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "enable band 7 failed", err);
   }
   else
   {
      /*disable band 7*/
      DCE_CHECK(dte->send_cmd(dte, "AT+QCFG=\"band\",0,80095,0,1\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
      DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "disable band 7 failed", err);
   }

   return ESP_OK;
err:
   return ESP_FAIL;
}


esp_err_t ec21_get_network_extended_info(modem_dce_t *dce )
{
   modem_dte_t *dte = ec21_dce->parent.dte;
   ec21_dce->parent.handle_line = ec21_handle_QNWINFO;

      DCE_CHECK(dte->send_cmd(dte, "AT+QNWINFO\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
      DCE_CHECK(ec21_dce->parent.state == MODEM_STATE_SUCCESS, "get QNWINFO failed", err);

   return ESP_OK;
err:
   return ESP_FAIL;
}
