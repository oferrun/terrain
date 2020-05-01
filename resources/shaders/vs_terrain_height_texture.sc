$input a_position, a_color1, i_data0, i_data1
$output v_position, v_texcoord0, v_bc

/*
 * Copyright 2015 Andrew Mac. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include "common.sh"

SAMPLER2D(s_heightTexture, 0);
// x - height scale
// y = "sea level"
uniform vec4 u_heightMapParams;

// displacement map
float dmap(vec2 pos)
{
	return (texture2DLod(s_heightTexture, pos , 0).x) * 1.0;
}

void main()
{
	float scale = i_data0.z;

	v_texcoord0.x = a_position.x;
	v_texcoord0.y = a_position.z;
	// 17 / 1024
	v_texcoord0 *= u_heightMapParams.z;
	v_position = a_position.xyz;
	v_position.x *= scale;
	v_position.z *= scale;
	float loadTransition = i_data0.w;

	v_texcoord0.xy += i_data1.xy;
	
	//v_texcoord0.xy = i_data1.xy;
	// left edge- id % 16 == 0
	// right edge - id % 16 == 15
	// top edge id >= 250
	// bottom edge id < 16
	
	if (loadTransition > 0.0)
	{
		int isLeftEdge = gl_VertexID  % 17;
		int isRightEdge = isLeftEdge;
		int isTopEdge = gl_VertexID;
		int isBottomEdge = gl_VertexID;

		if (isLeftEdge == 0)
		{
			int edgeIndex = gl_VertexID / 17;

			float f = loadTransition % 16;
			f = pow(2,f);
			float m = mod(edgeIndex, f);
			// s will hold 1 if verex needs to move, 0 otherwise
			float s = 1.0 - step(m, 0.5);
			
			v_position.z +=  (f - m) * s * 0.0625 * scale;
		
		}
		else if (isRightEdge == 16)
		{
			int edgeIndex = gl_VertexID / 17;
			// extract value from bits 4-7 in loadTranstion
			float f = floor(loadTransition / 16.0) % 16;
			f = pow(2,f);
			float m = mod(edgeIndex, f);
			// s will hold 1 if verex needs to move, 0 otherwise
			float s = 1.0 - step(m, 0.5);
			
			v_position.z +=  (f - m) * s * 0.0625 * scale;
		}
		else if (isTopEdge >= 272)
		{
			int edgeIndex = isTopEdge - 272;
			// extract value from bits 4-7 in loadTranstion
			float f = floor(loadTransition / 256.0) % 16;
			f = pow(2,f);
			float m = mod(edgeIndex, f);
			// s will hold 1 if verex needs to move, 0 otherwise
			float s = 1.0 - step(m, 0.5);
			
			v_position.x +=  (f - m) * s * 0.0625 * scale;
		}
		else if (isBottomEdge < 17)
		{
			int edgeIndex = isBottomEdge;
			// extract value from bits 4-7 in loadTranstion
			float f = floor(loadTransition / 4096.0) % 16;
			f = pow(2,f);
			float m = mod(edgeIndex, f);
			// s will hold 1 if verex needs to move, 0 otherwise
			float s = 1.0 - step(m, 0.5);
			
			v_position.x +=  (f - m) * s * 0.0625 * scale;
		}
		
	}


	//v_position.xz *= u_scale;
	v_bc = a_color1;

	v_position.y = dmap(v_texcoord0) * u_heightMapParams.x;
	vec4 worldPos = vec4(v_position.xyz, 1.0);
	worldPos.x += i_data0.x;
	worldPos.z += i_data0.y;
	v_position = worldPos;
	gl_Position = mul(u_viewProj, worldPos);
	
	//gl_Position /= (gl_VertexID == 54 ? 0 : 1);
}
