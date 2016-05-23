#ifndef DELEGATE_H
#define DELEGATE_H

#include <stdint.h>
#include <type_traits>
#include "core/assert.h"

namespace re {

template <typename F>
class delegate;

template <typename R, typename... A>
class delegate<R(A...)> {
  typedef R (*thunk_type)(void *, A...);

  template <typename T>
  struct const_member_data {
    T *callee;
    R (T::*func)(A...) const;
  };

  template <typename T>
  struct member_data {
    T *callee;
    R (T::*func)(A...);
  };

  typedef R (*func_data)(A...);

 public:
  delegate() : thunk_(nullptr), data_() {}

  delegate(std::nullptr_t) : delegate() {}

  template <typename T>
  delegate(T *callee, std::nullptr_t)
      : delegate() {}

  template <typename T>
  delegate(T *callee, R (T::*func)(A...) const)
      : delegate() {
    static_assert(sizeof(const_member_data<T>) < sizeof(data_),
                  "data not large enough to hold member function pointer");

    thunk_ = reinterpret_cast<thunk_type>(&const_member_thunk<T>);

    *reinterpret_cast<const_member_data<T> *>(data_) = {callee, func};
  }

  template <typename T>
  delegate(T *callee, R (T::*func)(A...))
      : delegate() {
    static_assert(sizeof(member_data<T>) < sizeof(data_),
                  "data not large enough to hold member function pointer");

    thunk_ = reinterpret_cast<thunk_type>(&member_thunk<T>);

    *reinterpret_cast<member_data<T> *>(data_) = {callee, func};
  }

  delegate(R (*func)(A...)) : delegate() {
    thunk_ = reinterpret_cast<thunk_type>(&func_thunk);

    *reinterpret_cast<func_data *>(data_) = func;
  }

  operator bool() const {
    return !!thunk_;
  }

  bool operator==(const delegate &rhs) const noexcept {
    return (thunk_ == rhs.thunk_) && !memcmp(data_, rhs.data_, sizeof(data_));
  }

  bool operator!=(const delegate &rhs) const noexcept {
    return !operator==(rhs);
  }

  R operator()(A... args) {
    DCHECK(thunk_);
    return thunk_(data_, args...);
  }

 private:
  template <typename T>
  static R const_member_thunk(const_member_data<T> *data, A... args) {
    DCHECK(data->callee && data->func);
    return (data->callee->*data->func)(args...);
  }

  template <typename T>
  static R member_thunk(member_data<T> *data, A... args) {
    DCHECK(data->callee && data->func);
    return (data->callee->*data->func)(args...);
  }

  static R func_thunk(func_data *data, A... args) {
    DCHECK(data);
    return (*data)(args...);
  }

  thunk_type thunk_;
  uint8_t data_[32];
};

template <typename T, typename R, typename... A>
delegate<R(A...)> make_delegate(R (T::*func)(A...) const, T *callee) {
  return delegate<R(A...)>(callee, func);
}

template <typename T, typename R, typename... A>
delegate<R(A...)> make_delegate(R (T::*func)(A...), T *callee) {
  return delegate<R(A...)>(callee, func);
}
template <typename R, typename... A>
delegate<R(A...)> make_delegate(R (*func)(A...)) {
  return delegate<R(A...)>(func);
}
}

#endif
