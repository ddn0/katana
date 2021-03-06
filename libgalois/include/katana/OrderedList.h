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

#ifndef KATANA_LIBGALOIS_KATANA_ORDEREDLIST_H_
#define KATANA_LIBGALOIS_KATANA_ORDEREDLIST_H_

#include "katana/FlatMap.h"
#include "katana/config.h"

namespace katana {

template <
    class Compare = std::less<int>, typename T = int, bool concurrent = true>
class OrderedList : private boost::noncopyable, private PaddedLock<concurrent> {
  typedef katana::flat_map<T, std::deque<T>, Compare> Map;

  Map map;

  using PaddedLock<concurrent>::lock;
  using PaddedLock<concurrent>::try_lock;
  using PaddedLock<concurrent>::unlock;

public:
  template <typename Tnew>
  using retype = OrderedList<Compare, Tnew, concurrent>;

  template <bool b>
  using rethread = OrderedList<Compare, T, b>;

  typedef T value_type;

  void push(value_type val) {
    lock();
    std::deque<T>& list = map[val];
    list.push_back(val);
    unlock();
  }

  template <typename Iter>
  void push(Iter b, Iter e) {
    lock();
    while (b != e) {
      std::deque<T>& list = map[*b];
      list.push_back(*b);
      ++b;
    }
    unlock();
  }

  template <typename RangeTy>
  void push_initial(RangeTy range) {
    if (ThreadPool::getTID() == 0)
      push(range.begin(), range.end());
  }

  katana::optional<value_type> pop() {
    lock();
    if (map.empty()) {
      unlock();
      return katana::optional<value_type>();
    }
    auto ii = map.begin();
    std::deque<T>& list = ii->second;
    katana::optional<value_type> v(list.front());
    list.pop_front();
    if (list.empty())
      map.erase(ii);
    unlock();
    return v;
  }
};
KATANA_WLCOMPILECHECK(OrderedList)

}  // namespace katana
#endif
