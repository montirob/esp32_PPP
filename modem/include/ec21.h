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
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../../modem/include/esp_modem_dce_service.h"
#include "../../modem/include/esp_modem.h"


typedef enum
{
   EC21_DTR_IGNORE,                     /**< TA ignores status on DTR  */
   EC21_DTR_CMD_MODE,                    /**< LowHigh on DTR: Change to command mode while remaining the connected call. */
   EC21_DTR_CMD_MODE_AND_DISCONNECT,    /**< LowHigh on DTR: Disconnect data call, and change  */
}ec21_dtrMode_t;
/**
 * @brief Create and initialize EC21 object
 *
 * @param dte Modem DTE object
 * @return modem_dce_t* Modem DCE object
 */
modem_dce_t *ec21_init(modem_dte_t *dte);

esp_err_t ec21_configure( modem_dce_t * dce );

esp_err_t ec21_get_module_info( modem_dce_t * dce );

esp_err_t ec21_enable_roaming( modem_dce_t * dce, bool isEnabled );

esp_err_t ec21_get_band7_state( modem_dce_t * dce, bool * isEnabled );

esp_err_t ec21_set_band7_state(modem_dce_t *ec21_dce, bool enable );

esp_err_t ec21_get_network_extended_info(modem_dce_t *dce );



#ifdef __cplusplus
}
#endif
