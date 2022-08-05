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

#ifndef OBJ_H
#define OBJ_H

/**
 * @file obj.h
 * @author Vasilis Mylonas <vasilismylonas@protonmail.com>
 * @brief OOP support library for C.
 * @version 0.1
 * @date 2022-04-24
 * @copyright Copyright (c) 2022 Vasilis Mylonas
 *
 * This library aims to provide support for object-oriented programming in C.
 */

#include <stddef.h>
#include <stdint.h>

/**
 * The maximum number of methods allowed for an object.
 */
#define OBJ_METHODS_MAX 64

/**
 * Marker for structs that want to be handled as libobj objects.
 *
 * This should be the first thing inside a struct. It may insert additional members inside the
 * struct which should not, however, be modified.
 */
#define OBJ_HEADER const struct __obj_vtable* __vptr;

/**
 * Initializes an objects' vtable.
 *
 * This must be the first thing in an initialization function. An initialization function acts like
 * a constructor in other programming languages. Its job is to initialize an initialized object.
 * They must accept a pointer to the uninitialized object as their first parameter under the name
 * 'self'.
 *
 * For example a file wrapper:
 *
 * @code
 *
 * int file_open(file_t* self, const char* path, int options) {
 *     OBJ_VTABLE_INIT(file_t, OBJ_METHOD(obj_destroy), OBJ_METHOD(obj_to_string));
 *
 *     self->fd = open(path, options);
 *     ...
 * }
 *
 * @endcode
 *
 * @param T The type of an object (this must be the typedef alias not the struct tag).
 * @param ... The method list for the type. This can be empty.
 *
 * @see OBJ_METHOD
 */
#define OBJ_VTABLE_INIT(T, ...)                                                                    \
    static const struct __obj_vtable __##T##_vtable = {                                            \
        ._private.size = sizeof(T),                                                                \
        ._private.name = #T,                                                                       \
        ._private.methods = {__VA_ARGS__},                                                         \
    };                                                                                             \
    self->__vptr = &__##T##_vtable;

/**
 * Creates a method entry.
 *
 * This is intended to be passed into OBJ_VTABLE_INIT in order to register a method with a type. To
 * call that method, use OBJ_CALL.
 *
 * @param function The function entry. The user must have declared/defined a (function)_impl
 *                 function which will be the actual implementation.
 *
 * @see OBJ_VTABLE_INIT
 * @see OBJ_CALL
 */
#define OBJ_METHOD(function)                                                                       \
    {                                                                                              \
        .name = #function, .impl = (void (*)(void))function##_impl,                                \
    }

/**
 * Calls a method on an object.
 *
 * Example:
 *
 * @code
 *
 * file_t file;
 * file_open(&file, "./temp.txt", FILE_RW);
 *
 * // Assuming file_t provides an implementation of stream_write_byte.
 * OBJ_CALL(int, stream_write_byte, &file, 255);
 *
 * @endcode
 *
 * Unlike the above example, a more proper use would be to implement wrapper functions like so:
 *
 * @code
 *
 * int stream_write_byte(obj_t* self, uint8_t byte) {
 *     return OBJ_CALL(int, stream_write_byte, self, byte);
 * }
 *
 * @endcode
 *
 * The user would then just have to call stream_write_byte as a normal function and internally it
 * would dispatch to the file_t implementation.
 *
 * If the specified method is not found, obj_on_missing_method() is called.
 *
 * @param TReturn The return type of the method to call.
 * @param name The method to call.
 * @param ... The arguments to pass to the method.
 * @return Whatever the called methods returns.
 *
 * @note The called method additionally receives a hidden last argument which is a string containing
 *       the name of the method.
 */
#define OBJ_CALL(TReturn, method, ...)                                                             \
    ((TReturn(*)())__obj_get_method(__LIBOBJ_FIRST(__VA_ARGS__), #method))(__VA_ARGS__, #method)

/**
 * Represents a libobj object.
 *
 * Although it has similar semantics to a void*, it is fundamentally different. A void* may point to
 * ANYTHING be it an int, a char or some other type. An obj_t* is expected to ALWAYS point to a
 * struct that begins with a properly initialized OBJ_HEADER or to be NULL.
 *
 * obj_t should be considered an incomplete type and any members are considered private.
 */
typedef const struct __obj_vtable* obj_t;

/**
 * Converts a T* to an obj_t*.
 *
 * This will result in a compile error if T is not a struct beginning with OBJ_HEADER or if T is
 * an incomplete type.
 *
 * To circumvent this (NOT RECOMMENDED) cast T* to a obj_t* first.
 */
#define OBJ(x) (&(x)->__vptr)

/**
 * Called when a requested method is not found.
 *
 * The default behavior is to print a message and call abort(). To restore the default behavior
 * set this back to NULL.
 *
 * @param object The object in question.
 * @param name The name of the requested method.
 */
extern void (*obj_on_missing_method)(const obj_t* object, const char* name);

/**
 * Returns a string describing an object's type.
 *
 * @param self The object.
 * @return The type name.
 */
const char* obj_typeof(const obj_t* self);

/**
 * Returns the size of an object.
 *
 * @param self The object.
 * @return The object's size.
 */
size_t obj_sizeof(const obj_t* self);

/**
 * Returns an identifier representing an object's type.
 *
 * @note Currently there is no way to guarantee that different type id's => different types (TODO).
 * It is however true that equal type id's => same type.
 *
 * @param self The object.
 * @return The type id.
 */
uintptr_t obj_typeid(const obj_t* self);

/**
 * Prints information about an object's vtable to stderr.
 *
 * This can be useful for debugging.
 *
 * @param self The object.
 */
void obj_print_vtable(const obj_t* self);

/**
 * Searches for a method with the specified name.
 *
 * @param self The object.
 * @param name The name of the method.
 * @return The method, or NULL if not found.
 */
void (*obj_find_method(const obj_t* self, const char* name))(void);

/**
 * Compares two objects.
 *
 * This can be specialized by the obj_cmp vtable entry. Default behavior is to compare via memcpy.
 *
 * @param self The object.
 * @param other The object to compare to.
 * @return The result of the comparison.
 */
int obj_cmp(const obj_t* self, const obj_t* other);

/**
 * Calls the destructor for an object.
 *
 * This can be specialized by the obj_destroy vtable entry. Default behavior is to do nothing.
 *
 * @param object The object.
 */
void obj_destroy(obj_t* object);

/**
 * Returns a string representation of an object.
 *
 * This can be specialized by the obj_to_string vtable entry. Default behavior is to return an
 * implementation-defined string.
 *
 * @param self The object.
 * @return The object's string representation. This is a malloc()'d C string and is owned by the
 *         caller of the function.
 */
char* obj_to_string(const obj_t* self);

#ifndef DOXYGEN
#define __LIBOBJ_FIRST(a, ...) a
void (*__obj_get_method(const obj_t*, const char*))(void);
struct __obj_vtable
{
    struct
    {
        size_t size;
        const char* name;
        struct
        {
            const char* name;
            void (*impl)(void);
        } methods[OBJ_METHODS_MAX];
    } _private;
};
#endif // DOXYGEN

#endif // OBJ_H
