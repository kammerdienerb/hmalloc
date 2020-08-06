#include "locks.h"
#include "internal.h"

HMALLOC_ALWAYS_INLINE
void mutex_init(mutex_t *mtx_ptr) {
    pthread_mutex_init(mtx_ptr, NULL);
}

HMALLOC_ALWAYS_INLINE
void spin_init(spin_t *spin_ptr) {
    pthread_spin_init(spin_ptr, PTHREAD_PROCESS_PRIVATE);
}


HMALLOC_ALWAYS_INLINE
void mutex_lock(mutex_t *mtx_ptr) {
    pthread_mutex_lock(mtx_ptr);
}

HMALLOC_ALWAYS_INLINE
void mutex_unlock(mutex_t *mtx_ptr) {
    pthread_mutex_unlock(mtx_ptr);
}

HMALLOC_ALWAYS_INLINE
void spin_lock(spin_t *spin_ptr) {
    pthread_spin_lock(spin_ptr);
}

HMALLOC_ALWAYS_INLINE
void spin_unlock(spin_t *spin_ptr) {
    pthread_spin_unlock(spin_ptr);
}
