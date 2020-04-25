/*
 * Copyright 2014 Stanlo Slasinski. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include "bgfx_compute.sh"

IMAGE2D_RW(s_texColor, r16f, 0);


NUM_THREADS(16, 16, 1)
void main()
{
	
	float color = 1.0;
	imageStore(s_texColor, gl_GlobalInvocationID.xy, color );
}
