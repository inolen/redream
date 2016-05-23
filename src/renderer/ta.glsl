static const char *ta_vp =
"uniform mat4 u_mvp;\n"

"layout(location = 0) in vec3 attr_xyz;\n"
"layout(location = 1) in vec2 attr_texcoord;\n"
"layout(location = 2) in vec4 attr_color;\n"
"layout(location = 3) in vec4 attr_offset_color;\n"

"out vec4 var_color;\n"
"out vec4 var_offset_color;\n"
"out vec2 var_diffuse_texcoord;\n"

"void main() {\n"
"	var_color = attr_color;\n"
"	var_offset_color = attr_offset_color;\n"
"	var_diffuse_texcoord = attr_texcoord;\n"

"	gl_Position = u_mvp * vec4(attr_xyz, 1.0);\n"

"	// z is in the range of -znear to +zfar, since the actual perspective divide\n"
"	// won't occur, do it manually\n"
"	gl_Position.z /= attr_xyz.z;\n"

"	// specify w so OpenGL applies perspective corrected texture mapping, but\n"
"	// cancel the perspective divide on the xyz, they're already perspective\n"
"	// correct\n"
"	float w = 1.0 / attr_xyz.z;\n"
"	gl_Position.xyz *= w;\n"
"	gl_Position.w = w;\n"
"}";

static const char *ta_fp =
"uniform sampler2D u_diffuse_map;\n"

"in vec4 var_color;\n"
"in vec4 var_offset_color;\n"
"in vec2 var_diffuse_texcoord;\n"

"layout(location = 0) out vec4 fragcolor;\n"

"void main() {\n"
"	vec4 color = var_color;\n"
"	color *= texture(u_diffuse_map, var_diffuse_texcoord);\n"
"	color += var_offset_color;\n"
"	fragcolor = color;\n"
"}";
