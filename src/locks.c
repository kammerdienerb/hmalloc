#include "locks.h"
#include "internal.h"

HMALLOC_ALWAYS_INLINE
internal inline void mutex_init(mutex_t *mtx_ptr) {
    pthread_mutex_init(mtx_ptr, NULL);
}

HMALLOC_ALWAYS_INLINE
internal inline void spin_init(spin_t *spin_ptr) {
    pthread_spin_init(spin_ptr, PTHREAD_PROCESS_PRIVATE);
}

HMALLOC_ALWAYS_INLINE
internal inline void rw_lock_init(rw_lock_t *rw_lock_ptr) {
    pthread_rwlock_init(rw_lock_ptr, NULL);
}


HMALLOC_ALWAYS_INLINE
internal inline void mutex_lock(mutex_t *mtx_ptr) {
    pthread_mutex_lock(mtx_ptr);
}

HMALLOC_ALWAYS_INLINE
internal inline void mutex_unlock(mutex_t *mtx_ptr) {
    pthread_mutex_unlock(mtx_ptr);
}

HMALLOC_ALWAYS_INLINE
internal inline void spin_lock(spin_t *spin_ptr) {
    pthread_spin_lock(spin_ptr);
}

HMALLOC_ALWAYS_INLINE
internal inline void spin_unlock(spin_t *spin_ptr) {
    pthread_spin_unlock(spin_ptr);
}

HMALLOC_ALWAYS_INLINE
internal inline void rw_lock_rlock(rw_lock_t *rw_lock_ptr) {
    pthread_rwlock_rdlock(rw_lock_ptr);
}

HMALLOC_ALWAYS_INLINE
internal inline void rw_lock_wlock(rw_lock_t *rw_lock_ptr) {
    pthread_rwlock_wrlock(rw_lock_ptr);
}

HMALLOC_ALWAYS_INLINE
internal inline void rw_lock_unlock(rw_lock_t *rw_lock_ptr) {
    pthread_rwlock_unlock(rw_lock_ptr);
}
