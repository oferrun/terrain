/*
 * Copyright 2014 Stanlo Slasinski. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include "bgfx_compute.sh"
#include "common.sh"

IMAGE2D_RW(s_height, r32f, 0);
//SAMPLER2D(s_depth, 0);
BUFFER_RO(u_mouseBuffer, vec4, 1);

uniform vec4 u_params;




inline float evaluateModificationBrush (    vec2             worldPosition,
                                            vec2             mousePosition,
                                            float                    size)
{
    float dist = length(worldPosition - mousePosition);
    dist /= size;
    return saturate(min(2-dist, 1.0 / (1.0 + pow(dist*2.0, 4))));

}

//gl_GlobalInvocationID

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
	const vec3 worldMousePosition = u_mouseBuffer[0].xyz;
	vec2 uv = vec2(gl_GlobalInvocationID.xy) / vec2(129.0, 129.0);
	const vec2 worldPosition_xz =  uv * 64.0;
	float brushSize = u_params.z;
	
	float displacement = evaluateModificationBrush(worldPosition_xz, worldMousePosition.xz, brushSize) * 0.01;
	float h = imageLoad(s_height,coord).x;
	
	//displacement = worldMousePosition.x / 64.0;

	imageStore(s_height, coord, h + displacement);
}
