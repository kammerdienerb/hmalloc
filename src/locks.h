#ifndef __LOCKS_H__
#define __LOCKS_H__

#include "internal.h"

typedef pthread_mutex_t    mutex_t;
typedef pthread_spinlock_t spin_t;
typedef pthread_rwlock_t   rw_lock_t;

#define MUTEX_INITIALIZER  PTHREAD_MUTEX_INITIALIZER

internal void mutex_init(mutex_t *mtx_ptr);
internal void spin_init(spin_t *spin_ptr);
internal void rw_lock_init(rw_lock_t *rw_lock_ptr);

internal void mutex_lock(mutex_t *mtx_ptr);
internal void mutex_unlock(mutex_t *mtx_ptr);
internal void spin_lock(spin_t *spin_ptr);
internal void spin_unlock(spin_t *spin_ptr);
internal void rw_lock_rlock(rw_lock_t *rw_lock_ptr);
internal void rw_lock_wlock(rw_lock_t *rw_lock_ptr);
internal void rw_lock_unlock(rw_lock_t *rw_lock_ptr);

#endif
