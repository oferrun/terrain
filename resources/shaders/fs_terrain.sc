$input v_position, v_texcoord0, v_bc

/*
 * Copyright 2015 Andrew Mac. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include "../common/common.sh"

SAMPLER2D(albedoTexture, 1);
uniform vec4 u_renderParams;

void main()
{
	//vec3 col = vec3(1.0, 0.0, 0.0);
	vec3 col = texture2D(albedoTexture, v_texcoord0).rgb;
	if (u_renderParams.x > 0.0)
	{
		vec3  wfColor   = vec3(0.0,0.0,0.0);
		float wfOpacity = 1.0;
		float thickness = 1.0;

		vec3 fw = abs(dFdx(v_bc)) + abs(dFdy(v_bc));
		vec3 val = smoothstep(vec3_splat(0.0), fw*thickness, v_bc);
		float edge = min(min(val.x, val.y), val.z); // Gets to 0.0 when close to edges.

		vec3 edgeCol = mix(col, wfColor, wfOpacity);
		col = mix(edgeCol, col, edge);
	}
	vec4 center = vec4(u_renderParams.y, 0.0, u_renderParams.w, 0.0);

	float dist = distance(center, v_position);
	if (dist > 5.0 && dist < 6.0 )
	{
		col = vec3(1.0, 0.0, 0.0);
	}
	gl_FragColor = vec4(col, 1.0);
}
