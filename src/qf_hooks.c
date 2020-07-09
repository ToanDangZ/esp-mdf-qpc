#include "qpc.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "priorityList.h"
#include "esp_log.h"
#include "sdkconfig.h"
#if defined(CONFIG_QPC_QSPY_PHY_BT_SPP)
#include "bt_bridge.h"
#elif defined(CONFIG_QPC_QSPY_PHY_UART)
#include "driver/uart.h"
#endif
#if defined(CONFIG_QPC_QSPY_PHY_WIFI)
#include "string.h"
#include "sys/status.h" /*Whitecat library*/
#include "socket.h"
#include "udp.h"
#include "inet.h"
#endif

/*TODO: remove this define*/
#define Q_SPY

#ifdef Q_SPY
static uint8_t qsTxBuf[CONFIG_QPC_QSPY_TX_SIZE];
static uint8_t qsRxBuf[CONFIG_QPC_QSPY_RX_SIZE];
//static QSTimeCtr QS_tickTime_;
//static QSTimeCtr QS_tickPeriod_;
#endif /* #ifdef Q_SPY */

static const char *TAG = "qf_hooks";
static const char *QSPY_TAG = "qspy";

#if defined(CONFIG_QPC_QSPY_PHY_UART)
#define RX_BUF_SIZE (1024)
#endif

#if defined(CONFIG_QPC_QSPY_PHY_WIFI)
#define QSPY_UDP_PORT "777"
#define QSPY_UDP_ADDR "*"
#define QSPY_UDP_TIMEOUT	(3.0) /*FIXME: timeout doesn't work, pooling is using*/
#endif

void QF_onStartup(void)
{
}

IRAM_ATTR void Q_onAssert(char_t const * const module, int_t location)
{
    ESP_LOGE(TAG, "Q_onAssert: module:%s loc:%d\n", module, location);
}

#ifdef Q_SPY
#define RD_BUF_SIZE (128)
uint8_t readBuffer[RD_BUF_SIZE];

static void _QSpyTask(void *pxParam)
{
    uint8_t *pRxData = NULL;
    size_t nRxData = 0;
    size_t idx = 0;
    uint16_t pktSize = 0;
    uint8_t const *pBlock;
    esp_err_t retval = ESP_OK;

#if defined(CONFIG_QPC_QSPY_PHY_BT_SPP)
    RingbufHandle_t RxBufHdl = NULL;
    while(1) {
        RxBufHdl = bt_bridge_get_rx_hdl();
        if(RxBufHdl == NULL) {
            vTaskDelay(1);
        } else {
            break;
        }
    }
#elif defined(CONFIG_QPC_QSPY_PHY_UART)
    pRxData = (uint8_t *)malloc(RX_BUF_SIZE);

    const uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, CONFIG_QPC_QSPY_TX_PIN, CONFIG_QPC_QSPY_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
#elif defined(CONFIG_QPC_QSPY_PHY_WIFI)
    struct sockaddr_storage addr;
    bool isUDPConnected = false;
    socklen_t addr_len = sizeof(addr);
    p_timeout tm;
    int err;
    size_t got;
    char addrstr[INET6_ADDRSTRLEN];
    char portstr[6];
    p_udp udp = malloc(sizeof(t_udp));

    if (udp == NULL)
    {
        panic("Failed to allocate udp socket");
    }
    udp->sock = SOCKET_INVALID;
    timeout_init(&udp->tm, -1, -1);
    udp->family = AF_UNSPEC;

    pRxData = (uint8_t *)malloc(UDP_DATAGRAMSIZE);
    if (pRxData == NULL)
    {
        panic("Failed to allocate pRxData");
    }
#endif

    ESP_LOGI(QSPY_TAG, "QSpy Task is up.");

    while(1) {
#ifdef CONFIG_QPC_QSPY_PHY_BT_SPP
        /* Check for receive data from BT */
        pRxData = (uint8_t *)xRingbufferReceiveUpTo(RxBufHdl, &nRxData, (TickType_t)(10 / portTICK_RATE_MS), 64);
        if(pRxData != NULL) {
            for(idx = 0; idx < nRxData; idx++) {
                QS_RX_PUT(pRxData[idx]);
            }
            vRingbufferReturnItem(RxBufHdl, (void*)pRxData);
            QS_rxParse();
        }
        /* Check if data needs to be tranmitted to BT */
        if(bt_bridge_rdy()) {
            if(retval == ESP_OK) {
                /* Only get new block when the previous block
                 * was successfully transmitted.
                 */
                pktSize = 64;
                QF_CRIT_ENTRY(&qfMutex);
                pBlock = QS_getBlock(&pktSize);
                QF_CRIT_EXIT(&qfMutex);
            }
            if(pktSize > 0) {
                retval = bt_bridge_send((uint8_t *)pBlock, pktSize);
            }
        }
#elif defined(CONFIG_QPC_QSPY_PHY_UART)
        nRxData = uart_read_bytes(UART_NUM_1, pRxData, RX_BUF_SIZE, (TickType_t)(10 / portTICK_RATE_MS));
        if(nRxData > 0) {
            for(idx = 0; idx < nRxData; idx++) {
                QS_RX_PUT(pRxData[idx]);
            }
            QS_rxParse();
        }

        pktSize = 64;
        QF_CRIT_ENTRY(&qfMutex);
        pBlock = QS_getBlock(&pktSize);
        QF_CRIT_EXIT(&qfMutex);

        if(pktSize > 0) {
            uart_write_bytes(UART_NUM_1, (char *)pBlock, pktSize);
        }
#elif defined(CONFIG_QPC_QSPY_PHY_WIFI)
        /*Open the connection only when the wifi is connected*/
        if (!NETWORK_AVAILABLE())
        {
            ESP_LOGD(QSPY_TAG, "Wifi is connecting");
            continue;
        }
        /*Open socket to receive data*/
        if (udp->family == AF_UNSPEC && udp->sock == SOCKET_INVALID) {
            const char *inet_err = inet_trycreate(&udp->sock, AF_INET, SOCK_DGRAM, 0);
            if (inet_err != NULL)
            {
                ESP_LOGE(QSPY_TAG, "%s", inet_err);
                panic("inet_trycreate failed");
            }
            socket_setnonblocking(&udp->sock);
            udp->family = AF_INET; /*IP4*/
            udp->tm.block = QSPY_UDP_TIMEOUT;
            //udp->tm.total = QSPY_UDP_TIMEOUT;

            /*Binding to address, port*/
            const char *port = QSPY_UDP_PORT;
            struct addrinfo bindhints;
            memset(&bindhints, 0, sizeof(bindhints));
            bindhints.ai_socktype = SOCK_DGRAM;
            bindhints.ai_family = udp->family;
            bindhints.ai_flags = AI_PASSIVE;
            inet_err = inet_trybind(&udp->sock,
                    &udp->family,
                    QSPY_UDP_ADDR,
                    port,
                    &bindhints);
            if (err) {
                ESP_LOGE(QSPY_TAG, "%s", inet_err);
                panic("inet_trybind failed");
            }
        }

        /*Receiving data*/
        memset(pRxData, 0, UDP_DATAGRAMSIZE);
        tm = &udp->tm;
        timeout_markstart(tm);
        got = 0; /*reset received data length*/
        err = socket_recvfrom(&udp->sock,
                (char *)pRxData,
                UDP_DATAGRAMSIZE,
                &got,
                (SA *)&addr,
                &addr_len,
                tm);
        if (err != IO_DONE && err != IO_CLOSED)
        {
            if (err == IO_CLOSED)
            {
                ESP_LOGE(QSPY_TAG, "refused");
            }
            /*FIXME: timeout error occurs every cycle*/
            else if (err != IO_TIMEOUT)
            {
                ESP_LOGE( QSPY_TAG, "%s", socket_strerror(err) );
            }

        }else
        {
            if (got > 0)
            {
                /*Debug purpose only*/
                err = getnameinfo((struct sockaddr *) &addr,
                        addr_len,
                        addrstr,
                        INET6_ADDRSTRLEN,
                        portstr,
                        6,
                        NI_NUMERICHOST | NI_NUMERICSERV);
                if (err)
                {
                    ESP_LOGE( QSPY_TAG, "%s", gai_strerror(err));
                }else
                {
                    ESP_LOGI(QSPY_TAG, "sender addr: %s, %s", addrstr, portstr);
                }
                isUDPConnected = true;
                for (idx = 0; idx < got; idx++)
                {
                    QS_RX_PUT(pRxData[idx]);
                }
                QS_rxParse();
            }
        }

        /*Only starting sending when there's any client connected*/
        if (isUDPConnected == true)
        {
            /*Sending if there's any data*/
            size_t sent = 0;
            pktSize = 64;
            QF_CRIT_ENTRY(&qfMutex);
            pBlock = QS_getBlock(&pktSize);
            QF_CRIT_EXIT(&qfMutex);

            if (pktSize > 0)
            {
                timeout_markstart(tm);
                err = socket_sendto(&udp->sock,
                        (char *)pBlock,
                        pktSize,
                        &sent,
                        (SA *) &addr,
                        addr_len,
                        tm);
                if (err != IO_DONE)
                {
                    if (err == IO_CLOSED)
                    {
                        ESP_LOGE(QSPY_TAG, "sending refused");
                    }
                    /*FIXME: timeout error occurs every cycle*/
                    else if (err != IO_TIMEOUT)
                    {
                        ESP_LOGE( QSPY_TAG, "sending %s", socket_strerror(err) );
                    }

                }
            }
        }

#else
#error "QSpy only support BT for this motor board"
#endif
    }
}

uint8_t QS_onStartup(void const *arg)
{
    QS_initBuf(qsTxBuf, sizeof(qsTxBuf));
    QS_rxInitBuf(qsRxBuf, sizeof(qsRxBuf));

    xTaskCreate(_QSpyTask, "QSpy", CONFIG_QPC_QSPY_STACK_SIZE, NULL, PRIORITY_QSPY, NULL);

    QS_filterOn(QS_UA_RECORDS);

    return (uint8_t)1; /* return success */
}


void QS_onCleanup(void)
{
    /// TODO: Implement functionality
}


IRAM_ATTR QSTimeCtr QS_onGetTime(void)
{
    return xTaskGetTickCount();
}


void QS_onFlush(void)
{
    /// TODO: Implement QS buffer flushing to HW peripheral
}


void QS_onReset(void)
{
    /// TODO: Implement functionality
}

void QS_onCommand(uint8_t cmdId, uint32_t param1, uint32_t param2, uint32_t param3)
{
    /// TODO: Implement functionality
}

#endif /*  Q_SPY */
