#include "init.h"
#include "internal_malloc.h"
#include "os.h"
#include "thread.h"

#include <stddef.h>
#include <stdlib.h>

internal void perform_sanity_checks(void) {
    chunk_header_t c;
    (void)c;

    ASSERT(sizeof(c) == 8, "chunk_header_t is invalid");
    ASSERT(IS_ALIGNED(sizeof(block_header_t), 8), "block header is misaligned");
}

internal void hmalloc_init(void) {
    const char *layout;

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

#ifdef HMALLOC_DO_LOGGING
            log_init();
#endif
            system_info_init();

            LOG("main thread has tid %d\n", get_this_tid());

            imalloc_init();

            /*
             * Figure out which layout strategy we should use for the
             * hmalloc_site_* API.
             */
            hmalloc_site_layout = HMALLOC_SITE_LAYOUT_SITE;
            layout              = getenv("HMALLOC_SITE_LAYOUT");
            if (layout) {
                if (strcmp(layout, "thread") == 0) {
                    hmalloc_site_layout = HMALLOC_SITE_LAYOUT_THREAD;
                    LOG("HMALLOC_SITE_LAYOUT = HMALLOC_SITE_LAYOUT_THREAD\n");
                } else if (strcmp(layout, "site") == 0) {
                    hmalloc_site_layout = HMALLOC_SITE_LAYOUT_SITE;
                    LOG("HMALLOC_SITE_LAYOUT = HMALLOC_SITE_LAYOUT_SITE\n");
                } else {
                    LOG("invalid value '%s' for HMALLOC_SITE_LAYOUT\n", layout);
                    ASSERT(0, "invalid HMALLOC_SITE_LAYOUT value");
                }
            } else {
                LOG("missing value for HMALLOC_SITE_LAYOUT -- defaulting to HMALLOC_SITE_LAYOUT_THREAD\n");
            }

            threads_init();

            user_heaps_init();

            hmalloc_is_initialized = 1;

        } INIT_UNLOCK();
    }
}

__attribute__((destructor))
internal void hmalloc_fini(void) {

    /*
     * After this point, we're going to stop servicing `free`s.
     * We're shutting down, so it'll be assumed that the rest of
     * our infrastructure isn't guaranteed to be operational.
     *
     *                                           - Brandon, 2019
     */
    hmalloc_ignore_frees = 1;
}
