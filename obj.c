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

#include "obj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void (*obj_on_missing_method)(const obj_t* object, const char* name);

void (*__obj_get_method(const obj_t* self, const char* name))(void)
{
    void (*method)(void) = obj_find_method(self, name);
    if (method != NULL)
    {
        return method;
    }

    if (obj_on_missing_method != NULL)
    {
        obj_on_missing_method(self, name);
    }
    else
    {
        fprintf(stderr,
                "Requested method '%s::%s()' does not exist on object <%s %p>\n",
                obj_typeof(self),
                name,
                obj_typeof(self),
                self);
    }

    abort();
}

void (*obj_find_method(const obj_t* self, const char* name))(void)
{
    for (size_t i = 0; i < OBJ_METHODS_MAX; i++)
    {
        if ((*self)->_private.methods[i].name == NULL)
        {
            break;
        }

        if (strcmp((*self)->_private.methods[i].name, name) == 0)
        {
            return (*self)->_private.methods[i].impl;
        }
    }

    return NULL;
}

const char* obj_typeof(const obj_t* self)
{
    return (*self)->_private.name;
}

size_t obj_sizeof(const obj_t* self)
{
    return (*self)->_private.size;
}

uintptr_t obj_typeid(const obj_t* self)
{
    return (uintptr_t)*self;
}

void obj_print_vtable(const obj_t* self)
{
    fprintf(stderr, "VTable for type %s:\n", obj_typeof(self));

    for (size_t i = 0; i < OBJ_METHODS_MAX; i++)
    {
        if ((*self)->_private.methods[i].name != NULL)
        {
            fprintf(stderr,
                    "  %s::%s() - %p\n",
                    obj_typeof(self),
                    (*self)->_private.methods[i].name,
                    (*self)->_private.methods[i].impl);
        }
    }
}

void obj_destroy(obj_t* self)
{
    void (*method)(void) = obj_find_method(self, __func__);
    if (method == NULL)
    {
        return;
    }

    ((void (*)(obj_t*))method)(self);
}

char* obj_to_string(const obj_t* self)
{
    void (*method)(void) = obj_find_method(self, __func__);
    if (method == NULL)
    {
        const char* type = obj_typeof(self);

        // return strdup(type)
        const size_t size = strlen(type) + 1;
        return memcpy(malloc(size), type, size);
    }

    return ((char* (*)(const obj_t*))method)(self);
}

int obj_cmp(const obj_t* self, const obj_t* other)
{
    const size_t self_size = obj_sizeof(self);
    const size_t other_size = obj_sizeof(other);

    if (self_size != other_size)
    {
        return self_size - other_size;
    }

    void (*method)(void) = obj_find_method(self, __func__);
    if (method == NULL)
    {
        return memcmp(self, other, self_size);
    }

    return ((int (*)(const obj_t*, const obj_t*, size_t))method)(self, other, self_size);
}
