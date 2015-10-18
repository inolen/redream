#ifndef SINGLETON_H
#define SINGLETON_H

namespace dreavm {

template <typename T>
class Singleton {
 public:
  static T &instance() {
    static T instance;
    return instance;
  }

 private:
  Singleton(){};
  Singleton(Singleton const &) = delete;
  void operator=(Singleton const &) = delete;
};
}

#endif
