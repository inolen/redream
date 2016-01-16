#ifndef MINMAX_HEAP_H
#define MINMAX_HEAP_H

#include <algorithm>
#include <type_traits>
#include "core/assert.h"

// Min-max heap implementation, based on
// http://www.akira.ruc.dk/~keld/teaching/algoritmedesign_f03/Artikler/02../Atkinson86.pdf

namespace dvm {

template <typename T>
static inline bool mmheap_is_max_level(T index) {
  T n = index + 1;
  T log2 = 0;
  while (n >>= 1) log2++;
  return log2 % 2 == 1;
}

template <typename T>
static inline T mmheap_parent(T index) {
  return (index - 1) / 2;
}

template <typename T>
static inline T mmheap_grandparent(T index) {
  return mmheap_parent(mmheap_parent(index));
}

template <typename T>
static inline bool mmheap_has_grandparent(T index) {
  return mmheap_parent(index) != 0;
}

template <typename T>
static inline T mmheap_left_child(T index) {
  return 2 * index + 1;
}

template <typename T>
static inline T mmheap_left_grandchild(T index) {
  return mmheap_left_child(mmheap_left_child(index));
}

template <typename T>
static inline T mmheap_is_child(T parent, T child) {
  return parent == ((child - 1) / 2);
}

template <typename RandomIt, typename Compare>
void mmheap_sift_up(
    RandomIt first, RandomIt last, Compare comp,
    typename std::iterator_traits<RandomIt>::difference_type index) {
  using difference_type =
      typename std::iterator_traits<RandomIt>::difference_type;

  // can't sift up past the root
  if (!index) {
    return;
  }

  difference_type ancestor_index = mmheap_parent(index);
  bool max_level = mmheap_is_max_level(ancestor_index);

  // if the node is smaller (greater) than its parent, then it is smaller
  // (greater) than all other nodes at max (min) levels up to the root. swap
  // the node with its parent and check min (max) levels up to the root until
  // the min-max order property is satisfied
  if (comp(*(first + index), *(first + ancestor_index)) ^ max_level) {
    std::swap(*(first + ancestor_index), *(first + index));
    index = ancestor_index;
  }
  // if the node is greater (smaller) than its parent, then it is greater
  // (smaller) than all other nodes at min (max) levels up to the root. the
  // node is in the correct order with regards to its parent, but check max
  // (min) levels up to the root until the min-max order property is satisfied
  else {
    max_level = !max_level;
  }

  while (mmheap_has_grandparent(index)) {
    ancestor_index = mmheap_grandparent(index);

    // once node is greater (smaller) than parent, the min-max order property
    // is satisfied
    if (!(comp(*(first + index), *(first + ancestor_index)) ^ max_level)) {
      break;
    }

    // swap node with parent
    std::swap(*(first + ancestor_index), *(first + index));
    index = ancestor_index;
  }
}

template <typename RandomIt, typename Compare>
void mmheap_sift_down(
    RandomIt first, RandomIt last, Compare comp,
    typename std::iterator_traits<RandomIt>::difference_type index) {
  using difference_type =
      typename std::iterator_traits<RandomIt>::difference_type;

  bool max_level = mmheap_is_max_level(index);
  difference_type size = last - first;

  while (index < size) {
    // get the smallest (largest) child or grandchild
    difference_type smallest = index;

    difference_type i = mmheap_left_child(index);
    difference_type end = std::min(i + 2, size);
    for (; i < end; i++) {
      if (comp(*(first + i), *(first + smallest)) ^ max_level) {
        smallest = i;
      }
    }

    i = mmheap_left_grandchild(index);
    end = std::min(i + 4, size);
    for (; i < end; i++) {
      if (comp(*(first + i), *(first + smallest)) ^ max_level) {
        smallest = i;
      }
    }

    // already the smallest (largest) node, nothing to do
    if (smallest == index) {
      break;
    }

    // swap the node with the smallest (largest) descendant
    std::swap(*(first + index), *(first + smallest));

    // if the swapped node was a child, then the current node, its child, and
    // its grandchild are all ordered correctly at this point satisfying the
    // min-max order property
    if (mmheap_is_child(index, smallest)) {
      break;
    }

    // if the node's new parent is now smaller than it, swap again
    if (comp(*(first + mmheap_parent(smallest)), *(first + smallest)) ^
        max_level) {
      std::swap(*(first + mmheap_parent(smallest)), *(first + smallest));
    }

    // if the swapped node was a grandchild, iteration must continue to
    // ensure it's now ordered with regard to its descendants
    index = smallest;
  }
}

template <typename RandomIt, typename Comp>
bool mmheap_validate(RandomIt first, RandomIt last, Comp comp) {
  using difference_type =
      typename std::iterator_traits<RandomIt>::difference_type;

  difference_type size = last - first;

  for (difference_type i = 0; i < size; i++) {
    bool flip_compare = mmheap_is_max_level(i);

    // values stored at nodes on even (odd) levels are smaller (greater) than
    // or equal to the values stored at their descendants

    // validate children
    difference_type j = std::min(mmheap_left_child(i), size);
    difference_type end = std::min(j + 2, size);

    for (; j < end; j++) {
      if (!(comp(*(first + i), *(first + j)) ^ flip_compare)) {
        return false;
      }
    }

    // validate grandchildren
    j = std::min(mmheap_left_grandchild(i), size);
    end = std::min(j + 4, size);

    for (; j < end; j++) {
      if (!(comp(*(first + i), *(first + j)) ^ flip_compare)) {
        return false;
      }
    }
  }

  return true;
}

template <typename RandomIt>
bool mmheap_validate(RandomIt first, RandomIt last) {
  return mmheap_validate(
      first, last,
      std::less<typename std::iterator_traits<RandomIt>::value_type>());
}

template <typename RandomIt, typename Comp>
void mmheap_push(RandomIt first, RandomIt last, Comp comp) {
  mmheap_sift_up(first, last, comp, (last - first) - 1);
}

template <typename RandomIt>
void mmheap_push(RandomIt first, RandomIt last) {
  mmheap_push(first, last,
              std::less<typename std::iterator_traits<RandomIt>::value_type>());
}

template <typename RandomIt, typename Comp>
RandomIt mmheap_find_min(RandomIt first, RandomIt last, Comp comp) {
  return first;
}

template <typename RandomIt>
RandomIt mmheap_find_min(RandomIt first, RandomIt last) {
  return mmheap_find_min(
      first, last,
      std::less<typename std::iterator_traits<RandomIt>::value_type>());
}

template <typename RandomIt, typename Comp>
RandomIt mmheap_find_max(RandomIt first, RandomIt last, Comp comp) {
  using difference_type =
      typename std::iterator_traits<RandomIt>::difference_type;

  difference_type size = last - first;

  if (size == 1) {
    // root must be the max
    return first;
  } else if (size == 2) {
    // root's child must be the max
    return first + 1;
  } else {
    // must be the larger of the two children
    if (comp(*(first + 1), *(first + 2))) {
      return first + 2;
    } else {
      return first + 1;
    }
  }
}

template <typename RandomIt>
RandomIt mmheap_find_max(RandomIt first, RandomIt last) {
  return mmheap_find_max(
      first, last,
      std::less<typename std::iterator_traits<RandomIt>::value_type>());
}

template <typename RandomIt, typename Comp>
void mmheap_pop_min(RandomIt first, RandomIt last, Comp comp) {
  if (first == last) {
    return;
  }

  RandomIt min = mmheap_find_min(first, last, comp);
  std::swap(*min, *--last);
  mmheap_sift_down(first, last, comp, std::distance(first, min));
}

template <typename RandomIt>
void mmheap_pop_min(RandomIt first, RandomIt last) {
  mmheap_pop_min(
      first, last,
      std::less<typename std::iterator_traits<RandomIt>::value_type>());
}

template <typename RandomIt, typename Comp>
void mmheap_pop_max(RandomIt first, RandomIt last, Comp comp) {
  if (first == last) {
    return;
  }

  RandomIt max = mmheap_find_max(first, last, comp);
  std::swap(*max, *--last);
  mmheap_sift_down(first, last, comp, std::distance(first, max));
}

template <typename RandomIt>
void mmheap_pop_max(RandomIt first, RandomIt last) {
  mmheap_pop_max(
      first, last,
      std::less<typename std::iterator_traits<RandomIt>::value_type>());
}
}

#endif
