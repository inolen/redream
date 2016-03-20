static const char *ta_vp = R"END(
uniform mat4 u_mvp;

layout(location = 0) in vec3 attr_xyz;
layout(location = 1) in vec2 attr_texcoord;
layout(location = 2) in vec4 attr_color;
layout(location = 3) in vec4 attr_offset_color;

out vec4 var_color;
out vec4 var_offset_color;
out vec2 var_diffuse_texcoord;

void main() {
	var_color = attr_color;
	var_offset_color = attr_offset_color;
	var_diffuse_texcoord = attr_texcoord;

	gl_Position = u_mvp * vec4(attr_xyz, 1.0);

	// z is in the range of -znear to +zfar, since the actual perspective divide
	// won't occur, do it manually
	gl_Position.z /= attr_xyz.z;

	// specify w so OpenGL applies perspective corrected texture mapping, but
	// cancel the perspective divide on the xyz, they're already perspective
	// correct
	float w = 1.0 / attr_xyz.z;
	gl_Position.xyz *= w;
	gl_Position.w = w;
}
)END";

static const char *ta_fp = R"END(
uniform sampler2D u_diffuse_map;

in vec4 var_color;
in vec4 var_offset_color;
in vec2 var_diffuse_texcoord;

layout(location = 0) out vec4 fragcolor;

void main() {
	vec4 color = var_color;
	color *= texture(u_diffuse_map, var_diffuse_texcoord);
	color += var_offset_color;
	fragcolor = color;
}
)END";
