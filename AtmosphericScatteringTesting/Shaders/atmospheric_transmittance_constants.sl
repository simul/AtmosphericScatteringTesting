//  Copyright (c) 2015 Simul Software Ltd. All rights reserved.
#ifndef TRANSMITTANCE_CONSTANTS_SL
#define TRANSMITTANCE_CONSTANTS_SL

SIMUL_CONSTANT_BUFFER(cbAtmosphere, 1)

uniform float		g_rayleighExpTerm;
uniform float		g_rayleighExpScale;
uniform float		g_rayleighLinearTerm;
uniform float		g_rayleighConstantTerm;

uniform vec3		g_rayleighScattering;
uniform float		g_rayleighDensity;

uniform float		g_mieExpTerm;
uniform float		g_mieExpScale;
uniform float		g_mieLinearTerm;
uniform float		g_mieConstantTerm;

uniform vec3		g_mieScattering;
uniform float		g_miePhaseFunction;

uniform vec3		g_mieExtinction;
uniform float		g_solarIrradiance;

uniform float		g_absorptionExpTerm;
uniform float		g_absorptionExpScale;
uniform float		g_absorptionLinearTerm;
uniform float		g_absorptionConstantTerm;

uniform vec3		g_absorptionExtinction;
uniform float		g_mu_s;

uniform float		g_bottomRadius;
uniform float		g_topRadius;
uniform float		g_mu_s_min;
uniform float		g_height;
SIMUL_CONSTANT_BUFFER_END

#endif