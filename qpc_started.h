#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "qpc.h"
#ifdef Q_SPY
#include "qs.h"
#endif /** Q_SPY */

void qpc_ini();
