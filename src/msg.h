#ifndef __MSG_H__
#define __MSG_H__

internal void msg_init(void);
internal void msg_fini(void);
internal void msg_obj_alloc(void* addr, u64 size, heap_t *heap);
internal void msg_obj_free(void* addr);

#endif
