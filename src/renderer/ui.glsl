static const char *ui_vp = R"END(
uniform mat4 u_mvp;

layout(location = 0) in vec2 attr_xy;
layout(location = 1) in vec4 attr_color;
layout(location = 2) in vec2 attr_texcoord;

out vec4 var_color;
out vec2 var_diffuse_texcoord;

void main() {
	var_color = attr_color;
	var_diffuse_texcoord = attr_texcoord;
	gl_Position = u_mvp * vec4(attr_xy, 0.0, 1.0);
}
)END";

static const char *ui_fp = R"END(
uniform sampler2D u_diffuse_map;

in vec4 var_color;
in vec2 var_diffuse_texcoord;

layout(location = 0) out vec4 fragcolor;

void main() {
	vec4 color = var_color;
	color *= texture(u_diffuse_map, var_diffuse_texcoord);
	fragcolor = color;
}
)END";
