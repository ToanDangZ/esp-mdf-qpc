#include "esp-mdf-qpc/qpc_started.h"

/*Include submodules*/
#include "app/test/test.h"
#include "app/signalList.h"

/*
 * small size pool.
 * size: Evt
 */
static QF_MPOOL_EL(QEvt) smallPoolSto[CONFIG_QPC_SMALL_POOL_SIZE];
/*
 * medium size pool
 * size: QEvt + 16 Bytes
 */
typedef struct {
    QEvt super;
    uint32_t data[4];
} medPool;
static QF_MPOOL_EL(medPool) medPoolSto[CONFIG_QPC_MEDIUM_POOL_SIZE];

/*
 * large size pool
 * size: QEvt + 32 Bytes
 */
typedef struct {
    QEvt super;
    uint32_t data[8];
} largePool;
static QF_MPOOL_EL(largePool) largePoolSto[CONFIG_QPC_LARGE_POOL_SIZE];
/*
 * Storage for Publish-Subscribe
 */
static QSubscrList subscrSto[MAX_PUB_SIG];

void qpc_ini()
{
    /* Initialize the framework */
    QF_init();

#ifdef Q_SPY
    QS_INIT((void*)0);
#endif /* Q_SPY */

    /* Initialize Event Pool
     * Note: QF can manage up to three event pools (e.g., small, medium, and large events).
     * An application may call this function up to three times to initialize up to three event
     * pools in QF.  The subsequent calls to QF_poolInit() function must be made with
     * progressively increasing values of the evtSize parameter.
     */
    QF_poolInit(smallPoolSto, sizeof(smallPoolSto), sizeof(smallPoolSto[0]));
    QF_poolInit(medPoolSto, sizeof(medPoolSto), sizeof(medPoolSto[0]));
    QF_poolInit(largePoolSto, sizeof(largePoolSto), sizeof(largePoolSto[0]));
    /* Initialize Publish-Subscribe */
    QF_psInit(subscrSto, Q_DIM(subscrSto));

    /* Call Active Object Constructors */
    TEST_ctor();

    /* Run QF */
    QF_run();
}

