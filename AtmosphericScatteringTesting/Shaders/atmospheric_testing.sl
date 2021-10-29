//  Copyright (c) 2015 Simul Software Ltd. All rights reserved.

#include "atmospheric_transmittance_constants.sl"

#ifndef ATMOSPHERICS_SL
#define ATMOSPHERICS_SL


float ClampCosine(float mu) {
	return clamp(mu, float(-1.0), float(1.0));
}

float ClampDistance(float d) {
	return max(d, 0.0);
}

float ClampRadius(float r) {
	return clamp(r, g_bottomRadius, g_topRadius);
}

float GetTextureCoordFromUnitRange(float x, int texture_size) {
	return 0.5 / float(texture_size) + x * (1.0 - 1.0 / float(texture_size));
}

float GetUnitRangeFromTextureCoord(float u, int texture_size) {
	return (u - 0.5 / float(texture_size)) / (1.0 - 1.0 / float(texture_size));
}


#endif