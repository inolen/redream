static const char *ta_vp =
"uniform vec4 u_video_scale;\n"

"layout(location = 0) in vec3 attr_xyz;\n"
"layout(location = 1) in vec2 attr_texcoord;\n"
"layout(location = 2) in vec4 attr_color;\n"
"layout(location = 3) in vec4 attr_offset_color;\n"

"out vec4 var_color;\n"
"out vec4 var_offset_color;\n"
"out vec2 var_texcoord;\n"

"void main() {\n"
"  var_color = attr_color;\n"
"  var_offset_color = attr_offset_color;\n"
"  var_texcoord = attr_texcoord;\n"

"  // scale x from [0,640] -> [-1,1] and y from [0,480] to [-1,1]\n"
"  gl_Position.xy = attr_xyz.xy * u_video_scale.xz + u_video_scale.yw;\n"

"  // the z coordinate is actually 1/w, convert to w. note, there is no\n"
"  // actual z coordinate provided to the ta, just 1/w. due to this, we set\n"
"  // z = w in the vertex shader such that the clip test always passes, and\n"
"  // then gl_FragDepth is manually set to w in the fragment shader\n"
"  gl_Position.zw = 1.0f / attr_xyz.zz;\n"

"  // cancel the perspective divide on the xy, they're already in ndc space\n"
"  gl_Position.xy *= gl_Position.w;\n"
"}";

static const char *ta_fp =
"uniform sampler2D u_diffuse;\n"
"uniform mediump float u_pt_alpha_ref;\n"

"in mediump vec4 var_color;\n"
"in mediump vec4 var_offset_color;\n"
"in mediump vec2 var_texcoord;\n"

"layout(location = 0) out mediump vec4 fragcolor;\n"

"void main() {\n"
"  mediump vec4 col = var_color;\n"
"  #ifdef IGNORE_ALPHA\n"
"    col.a = 1.0;\n"
"  #endif\n"
"  #ifdef TEXTURE\n"
"    mediump vec4 tex = texture(u_diffuse, var_texcoord);\n"
"    #ifdef IGNORE_TEXTURE_ALPHA\n"
"      tex.a = 1.0;\n"
"    #endif\n"
"    #ifdef PT_ALPHA_TEST\n"
"      if(tex.a < u_pt_alpha_ref)\n"
"        discard;\n"
"      fragcolor.a = 1.0f;\n"
"    #endif\n"
"    #ifdef SHADE_DECAL\n"
"      fragcolor = tex;\n"
"    #endif\n"
"    #ifdef SHADE_MODULATE\n"
"      fragcolor.rgb = tex.rgb * col.rgb;\n"
"      fragcolor.a = tex.a;\n"
"    #endif\n"
"    #ifdef SHADE_DECAL_ALPHA\n"
"      fragcolor.rgb = tex.rgb * tex.a + col.rgb * (1 - tex.a);\n"
"      fragcolor.a = col.a;\n"
"    #endif\n"
"    #ifdef SHADE_MODULATE_ALPHA\n"
"      fragcolor = tex * col;\n"
"    #endif\n"
"  #else\n"
"    fragcolor = col;\n"
"  #endif\n"
"  #ifdef OFFSET_COLOR\n"
"    fragcolor.rgb += var_offset_color.rgb;\n"
"  #endif\n"

"  // gl_FragCoord.w is 1/clip.w aka the original 1/w passed to the TA,\n"
"  // interpolated in screen space. this value is normally between [0,1],\n"
"  // however, values very close to the near plane often raise to 10-100000\n"

"  // unfortunately, there doesn't seem to exist a full 32-bit floating-point\n"
"  // depth buffer. because of this, the depth value written out here must be\n"
"  // normalized to [0,1] to satisfy OpenGL, which will then subsequently\n"
"  // quantize it to a 24-bit integer\n"

"  // if this value is normalized by (w - wmin) / (wmax - wmin), too much\n"
"  // precision is ultimately lost in small w values by the time the value is\n"
"  // written to the depth buffer. seeing that most values are between [0,1],\n"
"  // with only a few outliers being > 1, writing out log2(w) / log2(wmax)\n"
"  // works out well to preserve the precision in these smaller w values\n"

"  // note, 2^17 was chosen as ~100000 was largest value i'd seen passed as\n"
"  // the w component at the time this was written\n"

"  mediump float w = 1.0 / gl_FragCoord.w;\n"
"  gl_FragDepth = log2(1.0 + w) / 17.0;\n"

"  #ifdef DEBUG_DEPTH_BUFFER\n"
"    fragcolor.rgb = vec3(gl_FragDepth);\n"
"  #endif\n"
"}";
