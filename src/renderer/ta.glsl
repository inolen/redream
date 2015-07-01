static const char *ta_vp = R"END(
uniform vec2 u_xy_scale;

layout(location = 0) in vec3 attr_xyz;
layout(location = 1) in vec4 attr_color;
layout(location = 2) in vec4 attr_offset_color;
layout(location = 3) in vec2 attr_texcoord;

out vec4 var_color;
out vec4 var_offset_color;
out vec2 var_diffuse_texcoord;

void main() {
	var_color = attr_color;
	var_offset_color = attr_offset_color;
	var_diffuse_texcoord = attr_texcoord;
	gl_Position.x = (attr_xyz.x * u_xy_scale.x - 1.0);
	gl_Position.y = (1.0 - attr_xyz.y * u_xy_scale.y);
	gl_Position.z = attr_xyz.z;
	gl_Position.w = 1.0;
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
