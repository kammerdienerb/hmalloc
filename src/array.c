#include "array.h"
#include "internal_malloc.h"

array_t _array_make(int elem_size) {
    array_t a;

    memset(&a, 0, sizeof(a));
    a.elem_size = elem_size;
    a.capacity  = ARRAY_DEFAULT_CAP;

    return a;
}

array_t _array_make_with_cap(int elem_size, int initial_cap) {
    array_t a;

    memset(&a, 0, sizeof(a));
    a.elem_size = elem_size;
    a.capacity  = initial_cap;

    return a;
}

void _array_free(array_t *array) {
    if (array->data) {
        ifree(array->data);
    }
    memset(array, 0, sizeof(*array));
}

void _array_grow_if_needed(array_t *array) {
    void *data_save;
    int   grow;

    if (!array->data) {
        array->data = imalloc(array->capacity * array->elem_size);
    } else {
        grow = 0;

        while (array->used >= array->capacity) {
            array->capacity = next_power_of_2(array->capacity + 1);
            grow = 1;
        }

        if (grow) {
            data_save   = array->data;
            array->data = imalloc(array->capacity * array->elem_size);
            memcpy(array->data, data_save, array->used * array->elem_size);
            ifree(data_save);
        }
    }
}

void _array_grow_if_needed_to(array_t *array, int new_cap) {
    void *data_save;
    int   grow;

    grow = 0;
    if (new_cap > array->capacity) {
        grow = 1;
        array->capacity = next_power_of_2(new_cap);
    }

    if (!array->data) {
        array->data = imalloc(array->capacity * array->elem_size);
    } else {
        while (array->used >= array->capacity) {
            array->capacity = next_power_of_2(array->capacity + 1);
            grow = 1;
        }

        if (grow) {
            data_save   = array->data;
            array->data = imalloc(array->capacity * array->elem_size);
            memcpy(array->data, data_save, array->used * array->elem_size);
            ifree(data_save);
        }
    }
}

void * _array_next_elem(array_t *array) {
    void *elem_slot;

    _array_grow_if_needed(array);
    elem_slot = array->data + (array->elem_size * array->used++);
    return elem_slot;
}

void * _array_push(array_t *array, void *elem) {
    void *elem_slot;

    elem_slot = _array_next_elem(array);
    memcpy(elem_slot, elem, array->elem_size);

    return elem_slot;
}

void * _array_push_n(array_t *array, void *elems, int n) {
    void *elem_slot;

    if (unlikely(n == 0))    { return NULL; }

    _array_grow_if_needed_to(array, array->used + n);

    elem_slot    = array->data + (array->elem_size * array->used);
    array->used += n;

    memcpy(elem_slot, elems, n * array->elem_size);

    return elem_slot;
}

void * _array_insert(array_t *array, int idx, void *elem) {
    void *elem_slot;

    if (idx == array->used) {
        return _array_push(array, elem);
    }

    ASSERT(idx < array->used, "can't insert into arbitrary place in array");

    _array_grow_if_needed(array);

    elem_slot = array->data + (array->elem_size * idx);

    memmove(elem_slot + array->elem_size,
            elem_slot,
            array->elem_size * (array->used - idx));

    memcpy(elem_slot, elem, array->elem_size);

    array->used += 1;

    return elem_slot;
}

void _array_delete(array_t *array, int idx) {
    void *split;

    ASSERT(idx < array->used, "can't delete from arbitrary place in array");

    if (idx != array->used - 1) {
        split = array->data + (array->elem_size * idx);
        memmove(split,
                split + array->elem_size,
                array->elem_size * (array->used - idx - 1));
    }

    array->used -= 1;
}

void _array_zero_term(array_t *array) {
    _array_grow_if_needed(array);
    memset(array->data + (array->used * array->elem_size),
           0,
           array->elem_size);
}
