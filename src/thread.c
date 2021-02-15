#include "internal.h"
#include "thread.h"
#include "heap.h"
#include "os.h"
#include "init.h"

internal thread_data_t          *thread_datas;
internal mutex_t                 thread_datas_lock;
internal u32                     num_thrs;
internal __thread thread_data_t *local_thr;

internal void threads_init(void) {
    mutex_init(&thread_datas_lock);
    num_thrs = os_get_num_cpus() + 1;
    thread_datas = imalloc(sizeof(thread_data_t) * num_thrs);
    ASSERT(thread_datas != NULL, "failed to allocate thread data");
    LOG("using %u thread local heaps\n", num_thrs);
    LOG("initialized threads\n");
}

HMALLOC_ALWAYS_INLINE
internal inline hm_tid_t get_this_tid(void) {
    return OS_TID_TO_HM_TID(os_get_tid());
}

internal void dec_thr_ref(void *_thr) {
    thread_data_t *thr;

    thr = _thr;

    mutex_lock(&thread_datas_lock); {
        ASSERT(thr->is_valid, "why does an invalid thread data have a ref count?");
        ASSERT(thr->ref_count > 0, "invalid thread_data_t ref_count");
        thr->ref_count -= 1;
    } mutex_unlock(&thread_datas_lock);
}

HMALLOC_ALWAYS_INLINE
internal inline u32 get_next_cpu_idx(void) {
    u32 i;
    int all_same;
    u32 min_ref;
    u32 min_ref_idx;

    /* Try to find the thread_data_t with the lowest ref count. */
    all_same    = 1;
    min_ref     = thread_datas[0].ref_count;
    min_ref_idx = 0;
    for (i = 1; i < num_thrs; i += 1) {
        if (thread_datas[i].ref_count != min_ref) {
            all_same = 0;
        }
        if (thread_datas[i].ref_count < min_ref) {
            min_ref     = thread_datas[i].ref_count;
            min_ref_idx = i;
        }
    }

    if (all_same) { return 0; }

    return min_ref_idx;
}

HMALLOC_ALWAYS_INLINE
internal inline void setup_local_thr(void) {
    int            cpu_idx;
    thread_data_t *thr;
    hm_tid_t       tid;

    /* Ensure our system is initialized. */
    hmalloc_init();

    mutex_lock(&thread_datas_lock); {

        cpu_idx   = get_next_cpu_idx();
        local_thr = thr = thread_datas + cpu_idx;

        if (unlikely(!thr->is_valid)) {
            heap_make(&thr->heap);
            thr->heap.__meta.flags |= HEAP_THREAD;
            thr->is_valid           = 1;
            thr->id                 = cpu_idx;
            thr->ref_count          = 0;
            pthread_key_create(&thr->key, dec_thr_ref);
            pthread_setspecific(thr->key, (void*)thr);
            LOG("initialized a new thread with id %llu\n", thr->id);
        }

        tid                   = get_this_tid();
        thr->tid              = tid;
        thr->ref_count       += 1;
        thr->heap.__meta.tid  = tid;
        LOG("hid %d is a thread heap (tid = '%d')\n", thr->heap.__meta.hid, thr->tid);

    } mutex_unlock(&thread_datas_lock);

    local_thr = thr;
}

HMALLOC_ALWAYS_INLINE
internal inline thread_data_t * get_this_thread(void) {
    if (unlikely(local_thr == NULL)) {
        setup_local_thr();
        ASSERT(local_thr != NULL, "local_thr did not get set up");
    }

    return local_thr;
}

HMALLOC_ALWAYS_INLINE
internal inline heap_t * get_this_thread_heap(void) {
    return &get_this_thread()->heap;
}

HMALLOC_ALWAYS_INLINE
internal inline heap_t * get_user_heap(heap_handle_t handle) {
    /* Ensure our system is initialized. */
    hmalloc_init();
    return get_or_make_user_heap(handle);
}
