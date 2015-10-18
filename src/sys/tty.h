#ifndef TTY_H
#define TTY_H

namespace dreavm {
namespace sys {

class TTY {
 public:
  static TTY &instance();

  virtual ~TTY() {}

  virtual bool Init() = 0;

  virtual const char *Input() = 0;
  virtual void Print(const char *buffer) = 0;
};
}
}

#endif
