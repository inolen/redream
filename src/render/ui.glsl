static const char *ui_vp =
"uniform mat4 u_proj;\n"

"layout(location = 0) in vec2 attr_xy;\n"
"layout(location = 1) in vec2 attr_texcoord;\n"
"layout(location = 2) in vec4 attr_color;\n"

"out vec4 var_color;\n"
"out vec2 var_texcoord;\n"

"void main() {\n"
"  var_color = attr_color;\n"
"  var_texcoord = attr_texcoord;\n"
"  gl_Position = u_proj * vec4(attr_xy, 0.0, 1.0);\n"
"}";

static const char *ui_fp =
"uniform sampler2D u_diffuse;\n"

"in mediump vec4 var_color;\n"
"in mediump vec2 var_texcoord;\n"

"layout(location = 0) out mediump vec4 fragcolor;\n"

"void main() {\n"
"  mediump vec4 color = var_color;\n"
"  color *= texture(u_diffuse, var_texcoord);\n"
"  fragcolor = color;\n"
"}";
