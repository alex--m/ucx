/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2013.  ALL RIGHTS RESERVED.
* Copyright (C) Huawei Technologies Co., Ltd. 2020.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef INT_ARRAY_H_
#define INT_ARRAY_H_

#include <ucs/datastruct/ptr_array.h>

typedef uint64_t ucs_int_array_elem_t;


/**
 * A sparse array of indices.
 */
typedef ucs_ptr_array_t ucs_int_array_t;


/**
 * Initialize the array.
 *
 * @param [in] int_array          Pointer to a int array.
 * @param [in] name               The name of the int array.
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_init(ucs_int_array_t *int_array, const char *name)
{
    return ucs_ptr_array_init(int_array, name);
}


/**
 * Cleanup the array.
 *
 * @param [in] int_array  Pointer to a int array.
 * @param [in] leak_check Whether to check for leaks (elements which were not
 *                        freed from this int array).
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_cleanup(ucs_int_array_t *int_array, int leak_check)
{
    return ucs_ptr_array_cleanup(int_array, leak_check);
}


/**
 * Insert an index to the array.
 *
 * @param [in] int_array     Pointer to a int array.
 * @param [in] value         Value to insert. Must be 8-byte aligned.
 *
 * @return The index to which the value was inserted.
 *
 * Complexity: amortized O(1)
 *
 * @note The array will grow if needed.
 */
static UCS_F_ALWAYS_INLINE unsigned
ucs_int_array_insert(ucs_int_array_t *int_array, ucs_int_array_elem_t value)
{
    ucs_assert((value << 1) >> 1 == value);
    return ucs_ptr_array_insert(int_array, (void*)(value << 1));
}


/**
 * Insert an index to the array if not yet present there.
 *
 * @param [in] int_array     Pointer to a int array.
 * @param [in] value         Value to insert. Must be 8-byte aligned.
 * @param [in] assert_unique Assert the inserted items are unique to begin with.
 *
 * Complexity: O(n)
 *
 * @note The array will grow if needed.
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_insert_unique(ucs_int_array_t *int_array,
                            ucs_int_array_elem_t value,
                            int assert_unique)
{
    void *cmp;
    uint32_t idx;

    ucs_assert((value << 1) >> 1 == value);

    value <<= 1;
    ucs_ptr_array_for_each(cmp, idx, int_array) {
        if ((ucs_int_array_elem_t)cmp == value) {
            ucs_assert(!assert_unique);
            return;
        }
    }

    (void) ucs_ptr_array_insert(int_array, (void*)value);
}


/**
 * Allocate a number of contiguous array slots.
 *
 * @param [in] int_array      Pointer to a int array.
 * @param [in] element_count  Number of slots to allocate
 *
 * @return The index of the requested amount of slots (initialized to zero).
 *
 * Complexity: O(n*n) - not recommended for data-path
 *
 * @note The array will grow if needed.
 * @note Use @ref ucs_int_array_remove to "deallocate" the slots.
 */
static UCS_F_ALWAYS_INLINE unsigned
ucs_int_array_bulk_alloc(ucs_int_array_t *int_array, unsigned element_count)
{
    return ucs_ptr_array_bulk_alloc(int_array, element_count);
}



/**
 * Set a pointer in the array, overwriting the contents of the slot.
 *
 * @param [in] int_array      Pointer to a int array.
 * @param [in] element_index  Index of slot.
 * @param [in] new_val        Value to put into slot given by index.
 *
 * Complexity: O(n)
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_set(ucs_int_array_t *int_array, unsigned element_index,
                  ucs_int_array_elem_t new_val)
{
    return ucs_ptr_array_set(int_array, element_index, (void*)(new_val << 1));
}


/**
 * Remove a pointer from the array.
 *
 * @param [in] int_array      Pointer to a int array.
 * @param [in] element_index  Index to remove from.
 *
 * Complexity: O(1)
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_remove(ucs_int_array_t *int_array, unsigned element_index)
{
    ucs_ptr_array_remove(int_array, element_index);
}


/**
 * Replace pointer in the array, assuming the slot is occupied.
 *
 * @param [in] int_array      Pointer to a int array.
 * @param [in] element_index  Index of slot.
 * @param [in] new_val        Value to put into slot given by index.
 *
 * @return Old value of the slot
 */
static UCS_F_ALWAYS_INLINE void*
ucs_int_array_replace(ucs_int_array_t *int_array, unsigned element_index,
                      ucs_int_array_elem_t new_val)
{
    return ucs_ptr_array_replace(int_array, element_index,
                                 (void*)(new_val << 1));
}


/**
 * Get the current number of elements in the int array.
 *
 * @param [in] int_array      Pointer to a int array.
 *
 * @return Number of elements of the int array.
 */
static UCS_F_ALWAYS_INLINE unsigned
ucs_int_array_get_elem_count(const ucs_int_array_t *int_array)
{
    return ucs_ptr_array_get_elem_count(int_array);
}


/**
 * Check whether the int array is empty.
 *
 * @param [in] int_array      Pointer to a int array.
 *
 * @return Whether the int array is empty.
 */
static UCS_F_ALWAYS_INLINE int
ucs_int_array_is_empty(const ucs_int_array_t *int_array)
{
    return ucs_ptr_array_is_empty(int_array);
}


/**
 * Get an array element without checking is free.
 *
 * @param [in] _int_array      Pointer to a int array.
 * @param [in] _index          Index of slot
 *
 * @return 1 if the element is free and 0 if it's occupied.
 */
#define __ucs_int_array_get(_int_array, _index) \
    ((ucs_int_array_elem_t)((_int_array)->start[_index]))


/**
 * Check if element is free.
 *
 * @param [in] _elem        An element in the int array.
 *
 * @return 1 if the element is free and 0 if it's occupied.
 */
#define __ucs_int_array_is_free(_elem) __ucs_ptr_array_is_free(_elem)


/**
 * Get an array element without checking is free - and set a variable.
 *
 * @param [in] _int_array      Pointer to a int array.
 * @param [in] _index          Index of slot
 * @param [in] _var            Variable to set
 *
 * @return 1 if the element is free and 0 if it's occupied.
 */
#define __ucs_int_array_obtain(_int_array, _index, _var) \
    (((_var = (__ucs_int_array_get(_int_array, _index) >> 1)) << 1) | \
     __ucs_int_array_is_free(__ucs_int_array_get(_int_array, _index)))


/**
 * Get an array element, set a variable, and then return if it's free or not.
 *
 * @param [in] _int_array      Pointer to a int array.
 * @param [in] _index          Index of slot
 * @param [in] _var            Variable to set
 *
 * @return 1 if the element is free and 0 if it's occupied.
 */
#define __ucs_int_array_check(_int_array, _index, _var) \
    !__ucs_int_array_is_free(__ucs_int_array_obtain(_int_array, _index, _var))

/**
 * Retrieve a value from the array.
 *
 * @param [in]  _int_array  Pointer to a int array.
 * @param [in]  _index      Index to retrieve the value from.
 * @param [out] _var        Filled with the value.
 *
 * @return Whether the value is present and valid.
 *
 * Complexity: O(1)
 */
#define ucs_int_array_lookup(_int_array, _index, _var) \
    (ucs_unlikely((_index) >= (_int_array)->size) ? \
                    (UCS_V_INITIALIZED(_var), 0) : \
                    __ucs_int_array_check(_int_array, _index, _var))


/**
 * For-each user function: Calculates how many free elements are ahead.
 *
 * @param [in] int_array      Pointer to a int array.
 * @param [in] element_index  Index of slot
 *
 * @return size_elem - The number of free elements ahead if free, if not 1.
 */
static UCS_F_ALWAYS_INLINE uint32_t
__ucs_int_array_for_each_get_step_size(const ucs_int_array_t *int_array,
                                       unsigned element_index)
{
    return __ucs_ptr_array_for_each_get_step_size(int_array, element_index);
}


/**
 * Iterate over all valid elements in the array.
 *
 * @param [out] _var        Pointer to current array element in the foreach.
 * @param [out] _index      Index variable to use as iterator (unsigned).
 * @param [in]  _int_array  Pointer to a int array.
 */
#define ucs_int_array_for_each(_var, _index, _int_array) \
    for ((_index) = 0; ((_index) < (_int_array)->size); \
         (_index) += __ucs_int_array_for_each_get_step_size((_int_array), (_index))) \
         if (ucs_likely(__ucs_int_array_check(_int_array, _index, _var)))


/**
 *  Locked interface
 */


enum ucs_int_array_locked_flags {
    UCS_INT_ARRAY_LOCKED_FLAG_TRY_LOCK               = UCS_PTR_ARRAY_LOCKED_FLAG_TRY_LOCK,
    UCS_INT_ARRAY_LOCKED_FLAG_KEEP_LOCK_IF_FOUND     = UCS_PTR_ARRAY_LOCKED_FLAG_KEEP_LOCK_IF_FOUND,
    UCS_INT_ARRAY_LOCKED_FLAG_KEEP_LOCK_IF_NOT_FOUND = UCS_PTR_ARRAY_LOCKED_FLAG_KEEP_LOCK_IF_NOT_FOUND
};


/* Locked int array */
typedef ucs_ptr_array_locked_t ucs_int_array_locked_t;


/**
 * Locked array init
 *
 * @param [in] locked_int_array  Pointer to a locked int array.
 * @param [in] name              The name of the int array.
 *
 * @return Success or failure.
 */
static UCS_F_ALWAYS_INLINE ucs_status_t
ucs_int_array_locked_init(ucs_int_array_locked_t *locked_int_array,
                          const char *name)
{
    return ucs_ptr_array_locked_init(locked_int_array, name);
}


/**
 * Cleanup the locked array.
 *
 * @param [in] locked_int_array    Pointer to a locked int array.
 * @param [in] leak_check          Whether to check for leaks (elements which
 *                                 were not freed from this int array).
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_locked_cleanup(ucs_int_array_locked_t *locked_int_array,
                             int leak_check)
{
    ucs_ptr_array_locked_cleanup(locked_int_array, leak_check);
}


/**
 * Insert a pointer to the locked array.
 *
 * @param [in] locked_int_array  Pointer to a locked int array.
 * @param [in] value             Value to insert. Must be 8-byte aligned.
 *
 * @return The index to which the value was inserted.
 *
 * Complexity: Amortized O(1)
 *
 * @note The array will grow if needed.
 */
static UCS_F_ALWAYS_INLINE unsigned
ucs_int_array_locked_insert(ucs_int_array_locked_t *locked_int_array,
                            ucs_int_array_elem_t value)
{
    ucs_assert((value << 1) >> 1 == value);
    return ucs_ptr_array_locked_insert(locked_int_array, (void*)(value << 1));
}


/**
 * Allocate a number of contiguous slots in the locked array.
 *
 * @param [in] locked_int_array  Pointer to a locked int array.
 * @param [in] element_count     Number of slots to allocate
 *
 * @return The index of the requested amount of slots (initialized to zero).
 *
 * Complexity: O(n*n) - not recommended for data-path
 *
 * @note The array will grow if needed.
 * @note Use @ref ucs_int_array_locked_remove to "deallocate" the slots.
 */
static UCS_F_ALWAYS_INLINE unsigned
ucs_int_array_locked_bulk_alloc(ucs_int_array_locked_t *locked_int_array,
                                unsigned element_count)
{
    return ucs_ptr_array_locked_bulk_alloc(locked_int_array, element_count);
}


/**
 * Set a pointer in the array, overwriting the contents of the slot.
 *
 * @param [in] locked_int_array  Pointer to a locked int array.
 * @param [in] element_index     Index of slot.
 * @param [in] new_val           Value to put into slot given by index.
 *
 * Complexity: O(n)
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_locked_set(ucs_int_array_locked_t *locked_int_array,
                         unsigned element_index, ucs_int_array_elem_t new_val)
{
    ucs_ptr_array_locked_set(locked_int_array, element_index,
                             (void*)(new_val << 1));
}


/**
 * Set a pointer in the array, unless that slot is occupied - in which case
 * retrieve the existing contents. If the slot was not occupied - return the
 * value which was just set.
 *
 * @param [in]  locked_int_array  Pointer to a locked int array.
 * @param [in]  element_index     Index of slot.
 * @param [in]  new_val           Value to put into slot given by index.
 * @param [out] set_val           Value which now resides in said slot.
 *
 * Complexity: O(n)
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_locked_set_first(ucs_int_array_locked_t *locked_int_array,
                               unsigned element_index,
                               ucs_int_array_elem_t new_val,
                               ucs_int_array_elem_t *set_val)
{
    ucs_ptr_array_locked_set_first(locked_int_array, element_index,
                                   (void*)(new_val << 1), (void**)set_val);
    *set_val >>= 1;
}


/**
 * Remove a pointer from the locked array.
 *
 * @param [in] locked_int_array  Pointer to a locked int array.
 * @param [in] element_index     Index to remove from.
 *
 * Complexity: O(1)
 */
static UCS_F_ALWAYS_INLINE void
ucs_int_array_locked_remove(ucs_int_array_locked_t *locked_int_array,
                            unsigned element_index)
{
    ucs_ptr_array_locked_remove(locked_int_array, element_index);
}


/**
 * Replace pointer in the locked array, assuming the slot is occupied.
 *
 * @param [in] locked_int_array  Pointer to a locked int array.
 * @param [in] element_index     Index of slot.
 * @param [in] new_val           Value to put into slot given by index.
 *
 * @return Old value of the slot
 *
 * Complexity: O(1)
 */
static UCS_F_ALWAYS_INLINE ucs_int_array_elem_t
ucs_int_array_locked_replace(ucs_int_array_locked_t *locked_int_array,
                             unsigned element_index,
                             ucs_int_array_elem_t new_val)
{
    void *ptr_val = (void*)(new_val << 1);
    return (ucs_int_array_elem_t)ucs_ptr_array_locked_replace(locked_int_array,
                                                              element_index,
                                                              ptr_val);
}


/**
 * Acquire the int_array lock.
 *
 * @param [in] _locked_int_array  Pointer to a locked int array.
 */
#define ucs_int_array_locked_acquire_lock(_locked_int_array) \
    ucs_ptr_array_locked_acquire_lock(_locked_int_array)


/**
 * Try to acquire the int_array lock.
 *
 * @param [in] _locked_int_array  Pointer to a locked int array.
 */
#define ucs_int_array_locked_try_lock(_locked_int_array) \
    ucs_ptr_array_locked_try_lock(_locked_int_array)


/**
 * Release the int_array lock.
 *
 * @param [in] _locked_int_array  Pointer to a locked int array.
 */
#define ucs_int_array_locked_release_lock(_locked_int_array) \
    ucs_ptr_array_locked_release_lock(_locked_int_array)


/**
 * Retrieves a value from the locked array.
 *
 * @param [in]  locked_int_array   Pointer to a locked int array.
 * @param [in]  element_index      Index to retrieve the value from.
 * @param [in]  flags              From @ref enum ucs_int_array_locked_flags .
 * @param [out] var                Filled with the value.
 *
 * @return Whether the value is present and valid.
 *
 * Complexity: O(1)
 */
static UCS_F_ALWAYS_INLINE int
ucs_int_array_locked_lookup(ucs_int_array_locked_t *locked_int_array,
                            unsigned element_index, unsigned flags,
                            ucs_int_array_elem_t *var)
{
    int ret = ucs_ptr_array_locked_lookup(locked_int_array, element_index,
                                          flags, (void**)var);
    if (ret) {
        *var >>= 1;
    }

    return ret;
}


/**
 * Get the number of elements in the locked int array
 *
 * @param [in] locked_int_array      Pointer to a locked int array.
 *
 * @return Number of elements in the locked int array.
 */
static UCS_F_ALWAYS_INLINE unsigned
ucs_int_array_locked_get_elem_count(const ucs_int_array_locked_t *locked_int_array)
{
    return ucs_ptr_array_locked_get_elem_count(locked_int_array);
}


/**
 * Check whether the locked int array is empty.
 *
 * @param [in] int_array      Pointer to a locked int array.
 *
 * @return Whether the locked int array is empty.
 */
static UCS_F_ALWAYS_INLINE int
ucs_int_array_locked_is_empty(const ucs_int_array_locked_t *locked_int_array)
{
    return ucs_ptr_array_locked_is_empty(locked_int_array);
}


/**
 * If foreach locked int_array is finalized, releases lock.
 *
 * @param [in] locked_int_array   Pointer to a locked int array.
 * @param [in] element_index      The current for loop index.
 *
 * @return is_continue_loop for the for() loop end condition.
 */
static UCS_F_ALWAYS_INLINE int
__ucx_int_array_locked_foreach_finalize(ucs_int_array_locked_t *locked_int_array,
                                        uint32_t element_index)
{
    return __ucx_ptr_array_locked_foreach_finalize(locked_int_array,
                                                   element_index);
}


/**
 * Iterate over all valid elements in the locked array.
 *
 * Please notice that using break or return are not allowed in
 * this implementation.
 * Using break or return would require releasing the lock before by calling,
 * ucs_int_array_locked_release_lock(_locked_int_array);
 *
 * @param [out] _var                Pointer to current array element in the foreach.
 * @param [out] _index              Index variable to use as iterator (unsigned).
 * @param [in]  _locked_int_array   Pointer to a locked int array.
 */
#define ucs_int_array_locked_for_each(_var, _index, _locked_int_array) \
    for ((_index) = 0, \
         ucs_int_array_locked_acquire_lock(_locked_int_array); \
         __ucx_int_array_locked_foreach_finalize(_locked_int_array, (_index)); \
         (_index) += __ucs_int_array_for_each_get_step_size((&(_locked_int_array)->super), (_index))) \
        if (ucs_likely(__ucs_int_array_check(_int_array, _index, _var)))

#endif /* INT_ARRAY_H_ */
