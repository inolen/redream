#ifndef GLCONTEXT_H
#define GLCONTEXT_H

namespace dreavm {
namespace renderer {

class GLContext {
 public:
  virtual bool GLInit(int *width, int *height) = 0;
  virtual void GLSwapBuffers() = 0;
};
}
}

#endif
