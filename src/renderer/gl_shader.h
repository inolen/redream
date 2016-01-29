#ifndef GL_SHADER_H
#define GL_SHADER_H

#include <GL/glew.h>

namespace dvm {
namespace renderer {

enum UniformAttr {
  UNIFORM_MODELVIEWPROJECTIONMATRIX,
  UNIFORM_DIFFUSEMAP,
  UNIFORM_NUM_UNIFORMS
};

struct ShaderProgram {
  GLuint program;
  GLuint vertex_shader;
  GLuint fragment_shader;
  GLint uniforms[UNIFORM_NUM_UNIFORMS];
};

extern void PrintShaderInfoLog(GLuint shader);
extern bool CompileProgram(ShaderProgram *program, const char *header,
                           const char *vertexSource,
                           const char *fragmentSource);
extern void DestroyProgram(ShaderProgram *program);
}
}

#endif
