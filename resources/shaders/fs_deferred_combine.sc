$input v_texcoord0

/*
 * Copyright 2011-2020 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include <bgfx_shader.sh>
#include "shaderlib.sh"

SAMPLER2D(s_albedo, 0);

float LinearizeDepth(vec2 uv)
{
  float n = 0.1; // camera z near
  float f = 100.0; // camera z far
  float z = texture2D(s_albedo, uv).x;
  return (2.0 * n) / (f + n - z * (f - n));	
}

void main()
{
	//vec4 color  = texture2D(s_albedo, v_texcoord0);
	//float d = texture2D(s_albedo, v_texcoord0).x;
	float d = LinearizeDepth(v_texcoord0);
	vec4 color = vec4(d, d, d, 1.0);
	
	gl_FragColor = color;
}
