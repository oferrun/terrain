$input v_texcoord0

#include "bgfx_compute.sh"

/*
 * Copyright 2011-2020 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include <bgfx_shader.sh>
#include "shaderlib.sh"

uniform mat4 u_myInvViewProj;
uniform vec4 u_params;

SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_depth, 1);
BUFFER_RO(u_mouseBuffer, vec4, 2);


float LinearizeDepth(vec2 uv)
{
  float n = 0.1; // camera z near
  float f = 100.0; // camera z far
  float z = texture2D(s_albedo, uv).x;
  return (2.0 * n) / (f + n - z * (f - n));	
}


vec3 GetWorldPositionFromDepth (vec2 tid, float depth, vec2 invScreenSize)
{
    vec4 ndc;
    ndc.xy = tid;
    ndc.xy = ndc.xy * 2 - 1;
   // ndc.y *= -1;
    
    ndc.z = depth;
    ndc.w = 1;
    
    vec4 worldPosition = mul(u_myInvViewProj , ndc);
    worldPosition.xyz /= worldPosition.w;
   
    return worldPosition.xyz;
}

inline float evaluateModificationBrush (    vec2             worldPosition,
                                            vec2             mousePosition,
                                            float                    size)
{
    float dist = length(worldPosition - mousePosition);
    dist /= size;
    return saturate(min(2-dist, 1.0 / (1.0 + pow(dist*2.0, 4))));
}

void main()
{
	vec2 mousePos = u_mouseBuffer[0].xy;
	vec2 invScreenSize = u_params.xy;
	vec2 uv = mousePos * invScreenSize;
	const float mouseDepth = texture2D(s_depth, uv);

	
    
    const vec3 worldMousePosition = GetWorldPositionFromDepth (uv, mouseDepth, invScreenSize);

	const float pixelDepth = texture2D(s_depth, v_texcoord0);
	const float3 worldPosition = GetWorldPositionFromDepth (v_texcoord0, pixelDepth, invScreenSize);
    
	float brushSize = u_params.z;
	float brush = evaluateModificationBrush(worldPosition.xy, worldMousePosition.xy, brushSize);
	vec4 brushColor;
	// if left mouse button pressed - green. if right pressed red . none pressed - gray
	brushColor.xyz = u_mouseBuffer[0].z == 1 ? vec3(0.5, 0.9, 0.5) : u_mouseBuffer[0].z == 2 ? vec3(0.9, 0.5, 0.5) : vec3(0.5, 0.5, 0.7);
	brushColor.w = brush;
	vec4 color  = texture2D(s_albedo, v_texcoord0);

	//float d = LinearizeDepth(v_texcoord0);


	//color = vec4(brush, brush, brush, 1.0);
	//color.x *= u_mouseBuffer[0];
	color.xyz = mix(color.xyz, brushColor.xyz, brushColor.w);
	gl_FragColor = color;
}
