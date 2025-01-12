/**************************************************************
 * @file lfbb_impl.hpp
 * @brief A bipartite buffer implementation written in standard
 * c++11 suitable for both low-end microcontrollers all the way
 * to HPC machines. Lock-free for single consumer single
 * producer scenarios.
 * @version	1.2.0
 * @date 21. September 2022
 * @author Djordje Nedic
 **************************************************************/

/**************************************************************
 * Copyright (c) 2022 Djordje Nedic
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall
 * be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * This file is part of LFBB_CPP - Lock Free Bipartite Buffer
 *
 * Author:          Djordje Nedic <nedic.djordje2@gmail.com>
 * Version:         v1.2.0
 **************************************************************/

/************************** INCLUDE ***************************/

#include <algorithm>

/********************** PUBLIC METHODS ************************/

template <typename T, size_t size>
LfBb<T, size>::LfBb()
    : _r(0U), _w(0U), _i(0U), _write_wrapped(false), _read_wrapped(false) {}

template <typename T, size_t size>
T *LfBb<T, size>::WriteAcquire(const size_t free_required) {
    /* Preload variables with adequate memory ordering */
    const size_t w = _w.load(std::memory_order_relaxed);
    const size_t r = _r.load(std::memory_order_acquire);

    const size_t free = GetFree(w, r);
    const size_t linear_space = size - r;
    const size_t linear_free = std::min(free, linear_space);

    /* Try to find enough linear space until the end of the buffer */
    if (free_required <= linear_free) {
        return &_data[w];
    }

    /* If that doesn't work try from the beginning of the buffer */
    if (free_required <= free - linear_free) {
        _write_wrapped = true;
        return &_data[0];
    }

    /* Could not find free linear space with required size */
    return nullptr;
}

template <typename T, size_t size>
void LfBb<T, size>::WriteRelease(const size_t written) {
    /* Preload variables with adequate memory ordering */
    size_t w = _w.load(std::memory_order_relaxed);
    size_t i = _i.load(std::memory_order_relaxed);

    /* If the write wrapped set the invalidate index and reset write index*/
    if (_write_wrapped) {
        _write_wrapped = false;
        i = w;
        w = 0U;
    }

    // Increment the write index
    w += written;

    /* If we wrote over invalidated parts of the buffer move the invalidate
     * index
     */
    if (w > i) {
        i = w;
    }

    // Wrap to 0 if needed
    if (w == size) {
        w = 0U;
    }

    /* Store the indexes with adequate memory ordering */
    _i.store(i, std::memory_order_release);
    _w.store(w, std::memory_order_release);
}

template <typename T, size_t size>
std::pair<T *, size_t> LfBb<T, size>::ReadAcquire() {
    /* Preload variables with adequate memory ordering */
    const size_t w = _w.load(std::memory_order_acquire);
    const size_t i = _i.load(std::memory_order_acquire);
    const size_t r = _r.load(std::memory_order_relaxed);

    /* When read and write indexes are equal, the buffer is empty */
    if (r == w) {
        return std::make_pair(nullptr, 0U);
    }

    /* Simplest case, read index is behind the write index */
    if (r < w) {
        return std::make_pair(&_data[r], w - r);
    }

    /* Read index reached the invalidate index, make the read wrap */
    if (r == i) {
        _read_wrapped = true;
        return std::make_pair(&_data[0], w);
    }

    /* There is some data until the invalidate index */
    return std::make_pair(&_data[r], i - r);
}

template <typename T, size_t size>
void LfBb<T, size>::ReadRelease(const size_t read) {
    /* Preload variables with adequate memory ordering */
    size_t r = _r.load(std::memory_order_relaxed);

    /* If the read wrapped, overflow the read index */
    if (_read_wrapped) {
        _read_wrapped = false;
        r = 0U;
    }

    /* Increment the read index and wrap to 0 if needed */
    r += read;
    if (r == size) {
        r = 0U;
    }

    /* Store the indexes with adequate memory ordering */
    _r.store(r, std::memory_order_release);
}

/********************** std::span API *************************/
#if __cplusplus >= 202002L
template <typename T, size_t size>
std::span<T> LfBb<T, size>::WriteAcquireSpan(const size_t free_required) {
    auto res = WriteAcquire(free_required);
    if (res) {
        return {res, free_required};
    } else {
        return {res, 0};
    }
}

template <typename T, size_t size>
std::span<T> LfBb<T, size>::ReadAcquireSpan() {
    auto res = ReadAcquire();
    return {res.first, res.second};
}

template <typename T, size_t size>
void LfBb<T, size>::WriteRelease(const std::span<T> written) {
    WriteRelease(written.size());
}

template <typename T, size_t size>
void LfBb<T, size>::ReadRelease(const std::span<T> read) {
    ReadRelease(read.size());
}
#endif

/********************* PRIVATE METHODS ************************/

template <typename T, size_t size>
size_t LfBb<T, size>::GetFree(const size_t w, const size_t r) {
    if (r > w) {
        return (r - w) - 1U;
    } else {
        return (size - (w - r)) - 1U;
    }
}
