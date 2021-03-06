/*
 * This file belongs to the Galois project, a C++ library for exploiting
 * parallelism. The code is being released under the terms of the 3-Clause BSD
 * License (a copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2018, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 */

#pragma once

#include <atomic>
#include <type_traits>

#include "katana/config.h"

namespace katana {

template <typename T>
T
atomicMax(std::atomic<T>& a, const T& b) {
  T old_a = a.load(std::memory_order_relaxed);
  while (old_a < b &&
         !a.compare_exchange_weak(old_a, b, std::memory_order_relaxed))
    ;
  return old_a;
}

template <typename T>
T
atomicMin(std::atomic<T>& a, const T& b) {
  T old_a = a.load(std::memory_order_relaxed);
  while (old_a > b &&
         !a.compare_exchange_weak(old_a, b, std::memory_order_relaxed))
    ;
  return old_a;
}

#if __cplusplus > 201703L
template <typename T>
T
atomicAdd(std::atomic<T>& a, const T& b) {
  return a.fetch_add(b, std::memory_order_relaxed);
}
#else
template <typename T>
T
atomicAdd(
    std::atomic<T>& a, const T& b,
    std::enable_if_t<std::is_integral_v<T> >* = nullptr) {
  return a.fetch_add(b, std::memory_order_relaxed);
}

template <typename T>
T
atomicAdd(
    std::atomic<T>& a, const T& b,
    std::enable_if_t<!std::is_integral_v<T> >* = nullptr) {
  T old_a = a.load(std::memory_order_relaxed);
  while (!a.compare_exchange_weak(old_a, old_a + b, std::memory_order_relaxed))
    ;
  return old_a;
}
#endif

#if __cplusplus > 201703L
template <typename T>
T
atomicSub(std::atomic<T>& a, const T& b) {
  return a.fetch_sub(b, std::memory_order_relaxed);
}
#else
template <typename T>
T
atomicSub(
    std::atomic<T>& a, const T& b,
    std::enable_if_t<std::is_integral_v<T> >* = nullptr) {
  return a.fetch_sub(b, std::memory_order_relaxed);
}

template <typename T>
T
atomicSub(
    std::atomic<T>& a, const T& b,
    std::enable_if_t<!std::is_integral_v<T> >* = nullptr) {
  T old_a = a.load(std::memory_order_relaxed);
  while (!a.compare_exchange_weak(old_a, old_a - b, std::memory_order_relaxed))
    ;
  return old_a;
}
#endif

}  // end namespace katana
