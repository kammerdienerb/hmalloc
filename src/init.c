#include "init.h"
#include "os.h"
#include "thread.h"

#include <stddef.h>

internal void perform_sanity_checks(void) {
    chunk_header_t c;
    (void)c;

    ASSERT(sizeof(c) == 8, "chunk_header_t is invalid");

#ifdef HMALLOC_USE_SBLOCKS
    ASSERT(IS_POWER_OF_TWO(SBLOCK_SLOT_SIZE), "slot size is not power of two");
#endif
}

internal void hmalloc_init(void) {
    /*
     * Thread-unsafe check for performance.
     * Could give a false positive.
     */
    if (unlikely(!hmalloc_is_initialized)) {
        INIT_LOCK(); {
            /* Thread-safe check. */
            if (unlikely(hmalloc_is_initialized)) {
                INIT_UNLOCK();
                return;
            }

            perform_sanity_checks();

            hmalloc_is_initialized = 1;

#ifdef HMALLOC_DO_LOGGING
            log_init();
#endif
            system_info_init();
            threads_init();
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
