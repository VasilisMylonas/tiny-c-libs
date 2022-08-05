//
// The MIT License (MIT)
//
// Copyright (c)  2022 Vasilis Mylonas
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//

#include "vec.h"

#include <stdlib.h>
#include <string.h>

static void x_memswap(void* restrict a, void* restrict b, size_t size)
{
    if (a == b)
    {
        return;
    }

    char* temp[size];
    memcpy(temp, a, size);
    memcpy(a, b, size);
    memcpy(b, temp, size);
}

int _vec_create(void** self, size_t elem_size, size_t capacity)
{
    struct _vec_header* vec = malloc(sizeof(struct _vec_header) + VEC_DEFAULT_CAP * elem_size);

    if (vec == NULL)
    {
        return ENOMEM;
    }

    vec->cap = capacity == 0 ? VEC_DEFAULT_CAP : capacity;
    vec->size = 0;
    vec->elem_size = elem_size;

    *self = vec + 1;
    return 0;
}

size_t vec_size(void* self)
{
    return _VEC_HEADER(self)->size;
}

size_t vec_cap(void* self)
{
    return _VEC_HEADER(self)->cap;
}

void vec_clear(void* self)
{
    _VEC_HEADER(self)->size = 0;
}

void vec_destroy(void* self)
{
    free(_VEC_HEADER(self));
    *((void**)self) = NULL;
}

int vec_reserve(void* self, size_t elem_count)
{
    struct _vec_header* vec = _VEC_HEADER(self);
    size_t desired_size = vec->size + elem_count;
    size_t new_cap = vec->cap;

    if (desired_size < vec->cap)
    {
        return 0;
    }

    while (desired_size >= new_cap)
    {
        new_cap *= 2;
    }

    struct _vec_header* new_vec =
        realloc(vec, sizeof(struct _vec_header) + new_cap * vec->elem_size);
    if (new_vec == NULL)
    {
        return ENOMEM;
    }

    new_vec->cap = new_cap;
    *((void**)self) = new_vec + 1;

    return 0;
}

int vec_pack(void* self)
{
    struct _vec_header* vec = _VEC_HEADER(self);
    size_t new_cap = vec->size == 0 ? 1 : vec->size;

    struct _vec_header* new_vec =
        realloc(vec, sizeof(struct _vec_header) + new_cap * vec->elem_size);
    if (new_vec == NULL)
    {
        return ENOMEM;
    }

    new_vec->cap = new_cap;
    *((void**)self) = new_vec + 1;

    return 0;
}

int vec_dup(void* self, void* new)
{
    size_t size = vec_size(self);
    size_t elem_size = _VEC_HEADER(self)->elem_size;

    int result = _vec_create(new, elem_size, size);
    if (result != 0)
    {
        return result;
    }

    memcpy(*(void**)new, *(void**)self, size * elem_size);
    _VEC_HEADER(new)->size = size;
    _VEC_HEADER(new)->elem_size = elem_size;

    return 0;
}

int vec_cat(void* self, size_t count, const void* array)
{
    size_t elem_size = _VEC_HEADER(self)->elem_size;
    int result = vec_reserve(self, count);
    if (result != 0)
    {
        return result;
    }

    char* dest = ((char*)*(void**)self) + vec_size(self) * elem_size;
    memcpy(dest, array, count * elem_size);
    _VEC_HEADER(self)->size += count;

    return 0;
}

void vec_sort(void* self, int (*cmp_func)(const void*, const void*))
{
    qsort(*(void**)self, vec_size(self), _VEC_HEADER(self)->elem_size, cmp_func);
}

size_t vec_bsearch(void* self, const void* value, int (*cmp_func)(const void*, const void*))
{
    void* found =
        bsearch(value, *(void**)self, vec_size(self), _VEC_HEADER(self)->elem_size, cmp_func);

    if (found == NULL)
    {
        return VEC_NOT_FOUND;
    }

    return found - *(void**)self;
}

static void vec_shl(void* self)
{
    size_t elem_size = _VEC_HEADER(self)->elem_size;
    char* first = *(void**)self;
    size_t size = vec_size(self);

    memmove(first, first + elem_size, (size - 1) * elem_size);
}

static void vec_shr(void* self)
{
    size_t elem_size = _VEC_HEADER(self)->elem_size;
    char* first = *(void**)self;
    size_t size = vec_size(self);

    vec_reserve(self, 1);
    memmove(first + elem_size, first, (size - 1) * elem_size);
}

size_t vec_find(void* self, const void* value, int (*cmp_func)(const void*, const void*))
{
    char* first = *(void**)self;

    size_t size = vec_size(self);
    for (size_t i = 0; i < size; i++)
    {
        if (cmp_func(first, value) == 0)
        {
            return i;
        }

        first += _VEC_HEADER(self)->elem_size;
    }

    return VEC_NOT_FOUND;
}

size_t vec_rfind(void* self, const void* value, int (*cmp_func)(const void*, const void*))
{
    size_t size = vec_size(self);

    char* last = *(void**)self;
    last += (size - 1) * _VEC_HEADER(self)->elem_size;

    for (size_t i = size; i > 0; i--)
    {
        if (cmp_func(last, value) == 0)
        {
            return i - 1;
        }

        last -= _VEC_HEADER(self)->elem_size;
    }

    return VEC_NOT_FOUND;
}

void vec_reverse(void* self)
{
    size_t elem_size = _VEC_HEADER(self)->elem_size;
    size_t size = vec_size(self);
    char* first = *(void**)self;
    char* last = first + (size - 1) * elem_size;

    while (first < last)
    {
        x_memswap(first, last, elem_size);
        first += elem_size;
        last -= elem_size;
    }
}

void vec_rotl(void* self)
{
    size_t elem_size = _VEC_HEADER(self)->elem_size;
    size_t size = vec_size(self);

    char* first = *(void**)self;
    char* temp[elem_size];
    memcpy(temp, first, elem_size);

    vec_shl(self);
    memcpy(first + (size - 1) * elem_size, temp, elem_size);
}

void vec_rotr(void* self)
{
    size_t elem_size = _VEC_HEADER(self)->elem_size;
    size_t size = vec_size(self);

    char* last = ((char*)*(void**)self) + (size - 1) * elem_size;
    char* temp[elem_size];
    memcpy(temp, last, elem_size);

    vec_shr(self);
    memcpy(*(void**)self, temp, elem_size);
}

void vec_fill(void* self, const void* value)
{
    size_t elem_size = _VEC_HEADER(self)->elem_size;
    size_t size = vec_size(self);
    for (size_t i = 0; i < size; i++)
    {
        memcpy((char*)(*(void**)self) + i * elem_size, value, elem_size);
    }
}

int vec_push(void* self, const void* value)
{
    int result = vec_reserve(self, 1);
    if (result != 0)
    {
        return result;
    }

    size_t size = vec_size(self);
    size_t elem_size = _VEC_HEADER(self)->elem_size;
    memcpy((char*)(*(void**)self) + size * elem_size, value, elem_size);
    _VEC_HEADER(self)->size++;

    return 0;
}

bool vec_eq(void* self, void* other, int (*cmp_func)(const void* a, const void* b))
{
    size_t size1 = vec_size(self);
    size_t size2 = vec_size(other);

    if (size1 != size2)
    {
        return false;
    }

    size_t elem_size = _VEC_HEADER(self)->elem_size;

    for (size_t i = 0; i < size1; i++)
    {
        if (cmp_func(&(*(char**)self)[i * elem_size], &(*(char**)other)[i * elem_size]) != 0)
        {
            return false;
        }
    }

    return true;
}
