#include "init.h"
#include "os.h"
#include "thread.h"

/* __attribute__((constructor)) */
internal void hmalloc_init(void) {
    /*
     * Thread-unsafe check for performance.
     * Could give a false positive.
     */
    if (!hmalloc_is_initialized) {
        INIT_LOCK(); {
            /* Thread-safe check. */
            if (hmalloc_is_initialized) {
                INIT_UNLOCK();
                return;
            }

            hmalloc_is_initialized = 1;

            system_info_init();
            threads_init();
            /* thread_local_init(); */
            /* main_thread_init(); */
        } INIT_UNLOCK();
    }
}

__attribute__((destructor))
internal void hmalloc_fini(void) {
    /* INIT_LOCK(); { */
    /*     if (!hmalloc_is_initialized) { */
    /*         INIT_UNLOCK(); */
    /*         return; */
    /*     } */

    /*     thread_local_fini(); */

    /*     hmalloc_is_initialized = 0; */

    /*     LOG("hmalloc shut down\n"); */
    /* } INIT_UNLOCK(); */
}
