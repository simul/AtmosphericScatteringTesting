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

float GetLayerDensity(float exp_term, float exp_scale, float linear_term, float constant_term, float altitude)
{
	float density = exp_term * exp(exp_scale * altitude) + linear_term * altitude + constant_term;
	return clamp(density, 0.f, 1.f);
}

float RayleighPhaseFunction(float nu) {
	float k = 3.0 / (16.0 * PI);
	return k * (1.0 + nu * nu);
}

float MiePhaseFunction(float g, float nu) {
	float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
	return k * (1.0 + nu * nu) / pow(1.0 + g * g - 2.0 * g * nu, 1.5);
}

float DistanceToTopAtmosphereBoundary(float r, float mu) {
	//assert(r <= g_topRadius);
	//assert(mu >= -1.0 && mu <= 1.0);

	float discriminant = r * r * (mu * mu - 1.0) + (g_topRadius * g_topRadius);
	return ClampDistance(-r * mu + sqrt(discriminant));
}

float DistanceToBottomAtmosphereBoundary(float r, float mu) {
	//assert(r >= atmosphere.bottom_radius);
	//assert(mu >= -1.0 && mu <= 1.0);
	float discriminant = r * r * (mu * mu - 1.0) + (g_bottomRadius * g_bottomRadius);
	return ClampDistance(-r * mu - sqrt(discriminant));
}

float DistanceToNearestAtmosphereBoundary(float r, float mu, bool ray_r_mu_intersects_ground) {
	if (ray_r_mu_intersects_ground) {
		return DistanceToBottomAtmosphereBoundary(r, mu);
	}
	else {
		return DistanceToTopAtmosphereBoundary(r, mu);
	}
}

vec2 GetRMuFromTransmittanceTextureUv(vec2 uv) {
	//assert(uv.x >= 0.0 && uv.x <= 1.0);
	//assert(uv.y >= 0.0 && uv.y <= 1.0);
	float r, mu;

	float x_r = GetUnitRangeFromTextureCoord(uv.y, 256);
	float x_mu = GetUnitRangeFromTextureCoord(uv.x, 256);
	// Distance to top atmosphere boundary for a horizontal ray at ground level.
	float H = sqrt(g_topRadius * g_topRadius - g_bottomRadius * g_bottomRadius);
	// Distance to the horizon, from which we can compute r:
	float rho = H * x_r;
	r = sqrt(rho * rho + g_bottomRadius * g_bottomRadius);// (x_r * (g_topRadius - g_bottomRadius)) + g_bottomRadius;//
	// Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
	// and maximum values over all mu - obtained for (r,1) and (r,mu_horizon) -
	// 
	// from which we can recover mu:
	float d_min = g_topRadius - r;
	float d_max = rho + H;
	float d = d_min + x_mu * (d_max - d_min);
	mu = d == 0.0 ? float(1.0) : (H * H - rho * rho - d * d) / (2.0 * r * d);
	mu = ClampCosine(mu);

	return vec2(r, mu);
}

vec2 GetTransmittanceTextureUvFromRMu(float r, float mu)
{
	//assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
	//assert(mu >= -1.0 && mu <= 1.0);
	// Distance to top atmosphere boundary for a horizontal ray at ground level.
	float H = sqrt(g_topRadius * g_topRadius - g_bottomRadius * g_bottomRadius);
	// Distance to the horizon.
	float rho = sqrt(r * r - g_bottomRadius * g_bottomRadius);
	// Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
	// and maximum values over all mu - obtained for (r,1) and (r,mu_horizon).
	float d = DistanceToTopAtmosphereBoundary(r, mu);
	float d_min = g_topRadius - r;
	float d_max = rho + H;
	float x_mu = (d - d_min) / (d_max - d_min);
	float x_r = rho / H;//(r - g_bottomRadius) / (g_topRadius - g_bottomRadius);//
	return vec2(x_mu, x_r);
}


vec4 GetRMuMuSNuFromScatteringTextureUvwz(vec4 uvwz) {
	//assert(uvwz.x >= 0.0 && uvwz.x <= 1.0);
	//assert(uvwz.y >= 0.0 && uvwz.y <= 1.0);
	//assert(uvwz.z >= 0.0 && uvwz.z <= 1.0);
	//assert(uvwz.w >= 0.0 && uvwz.w <= 1.0);

	float r; // returned as negative if 'ray_intersects_ground' is false.
	float mu;
	float mu_s;
	float nu;

	bool ray_r_mu_intersects_ground;

	// Distance to top atmosphere boundary for a horizontal ray at ground level.
	float H = sqrt(g_topRadius * g_topRadius - g_bottomRadius * g_bottomRadius);
	// Distance to the horizon.
	float rho = H * GetUnitRangeFromTextureCoord(uvwz.w, 32);
	r = sqrt(rho * rho + g_bottomRadius * g_bottomRadius);

	if (uvwz.z < 0.5) {
		// Distance to the ground for the ray (r,mu), and its minimum and maximum
		// values over all mu - obtained for (r,-1) and (r,mu_horizon) - from which
		// we can recover mu:
		float d_min = r - g_bottomRadius;
		float d_max = rho;
		float d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(1.0 - 2.0 * uvwz.z, 128 / 2);
		mu = d == 0.0 ? float(-1.0) : ClampCosine(-(rho * rho + d * d) / (2.0 * r * d));
		ray_r_mu_intersects_ground = true;
	}
	else {
		// Distance to the top atmosphere boundary for the ray (r,mu), and its
		// minimum and maximum values over all mu - obtained for (r,1) and
		// (r,mu_horizon) - from which we can recover mu:
		float d_min = g_topRadius - r;
		float d_max = rho + H;
		float d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(2.0 * uvwz.z - 1.0, 128 / 2);
		mu = d == 0.0 ? float(1.0) : ClampCosine((H * H - rho * rho - d * d) / (2.0 * r * d));
		ray_r_mu_intersects_ground = false;
	}

	float x_mu_s = GetUnitRangeFromTextureCoord(uvwz.y, 32);
	float d_min = g_topRadius - g_bottomRadius;
	float d_max = H;
	float D = DistanceToTopAtmosphereBoundary(g_bottomRadius, g_mu_s_min);
	float A = (D - d_min) / (d_max - d_min);
	float a = (A - x_mu_s * A) / (1.0 + x_mu_s * A);
	float d = d_min + min(a, A) * (d_max - d_min);
	mu_s = d == 0.0 ? float(1.0) : ClampCosine((H * H - d * d) / (2.0 * g_bottomRadius * d));

	nu = ClampCosine(uvwz.x * 2.0 - 1.0);

	if (!ray_r_mu_intersects_ground)	
		r *= -1;

	return vec4(r, mu, mu_s, nu);
}

#endif