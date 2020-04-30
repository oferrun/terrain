/*
 * Copyright 2014 Stanlo Slasinski. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include "bgfx_compute.sh"
#include "common.sh"

//IMAGE2D_RO(s_depth, r8, 0);
SAMPLER2D(s_depth, 0);
BUFFER_WR(u_mouseBuffer, vec4, 1);


uniform mat4 u_myInvViewProj;
uniform vec4 u_params;



vec3 GetWorldPositionFromDepth (vec2 tid, float depth)
{
    vec4 ndc;
    ndc.xy = tid;
    ndc.xy = ndc.xy * 2 - 1;
    ndc.y *= -1;
    
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

NUM_THREADS(1, 1, 1)
void main()
{
	vec2 mousePosScreen = u_params.xy;
	
	//const float mouseDepth = imageLoad(s_depth, mousePosScreen);
	const float mouseDepth = texture2DLod(s_depth, mousePosScreen, 0);


    const vec3 worldMousePosition = GetWorldPositionFromDepth (mousePosScreen, mouseDepth);
	u_mouseBuffer[0] = vec4(worldMousePosition, 1);
}
