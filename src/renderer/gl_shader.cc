#include "core/core.h"
#include "renderer/gl_shader.h"

namespace dreavm {
namespace renderer {

#define GLSL_VERSION 330

// must match order of UniformAttr enum
static const char *uniform_names[] = {"u_mvp",  //
                                      "u_diffuse_map"};

static bool CompileShader(const char *source, GLenum shaderType,
                          GLuint *shader);

void PrintShaderInfoLog(GLuint shader) {
  int maxLength, length;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

  char *infoLog = new char[maxLength];
  glGetShaderInfoLog(shader, maxLength, &length, infoLog);
  LOG(INFO) << infoLog;
  delete[] infoLog;
  infoLog = nullptr;
}

bool CompileProgram(ShaderProgram *program, const char *header,
                    const char *vertexSource, const char *fragmentSource) {
  char buffer[16384] = {0};

  memset(program, 0, sizeof(*program));
  program->program = glCreateProgram();

  if (vertexSource) {
    snprintf(buffer, sizeof(buffer) - 1, "#version %d\n%s%s", GLSL_VERSION,
             header ? header : "", vertexSource);
    buffer[sizeof(buffer) - 1] = 0;

    if (!CompileShader(buffer, GL_VERTEX_SHADER, &program->vertex_shader)) {
      DestroyProgram(program);
      return false;
    }

    glAttachShader(program->program, program->vertex_shader);
  }

  if (fragmentSource) {
    snprintf(buffer, sizeof(buffer) - 1, "#version %d\n%s%s", GLSL_VERSION,
             header ? header : "", fragmentSource);
    buffer[sizeof(buffer) - 1] = 0;

    if (!CompileShader(buffer, GL_FRAGMENT_SHADER, &program->fragment_shader)) {
      DestroyProgram(program);
      return false;
    }

    glAttachShader(program->program, program->fragment_shader);
  }

  glLinkProgram(program->program);

  GLint linked;
  glGetProgramiv(program->program, GL_LINK_STATUS, &linked);

  if (!linked) {
    DestroyProgram(program);
    return false;
  }

  for (int i = 0; i < UNIFORM_NUM_UNIFORMS; i++) {
    program->uniforms[i] =
        glGetUniformLocation(program->program, uniform_names[i]);
  }

  return true;
}

bool CompileShader(const char *source, GLenum shaderType, GLuint *shader) {
  size_t sourceLength = strlen(source);

  *shader = glCreateShader(shaderType);
  glShaderSource(*shader, 1, (const GLchar **)&source,
                 (const GLint *)&sourceLength);
  glCompileShader(*shader);

  GLint compiled;
  glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

  if (!compiled) {
    PrintShaderInfoLog(*shader);
    glDeleteShader(*shader);
    return false;
  }

  return true;
}

void DestroyProgram(ShaderProgram *program) {
  if (program->vertex_shader > 0) {
    glDeleteShader(program->vertex_shader);
  }

  if (program->fragment_shader > 0) {
    glDeleteShader(program->fragment_shader);
  }

  glDeleteProgram(program->program);
}
}
}
