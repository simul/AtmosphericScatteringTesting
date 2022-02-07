//  Copyright (c) 2015 Simul Software Ltd. All rights reserved.


#include "atmospheric_transmittance_constants.sl"

#ifndef ATMOSPHERICS_SL
#define ATMOSPHERICS_SL

#define PI 3.14159265

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

bool RayIntersectsGround(float r, float mu) {
	//assert(r >= atmosphere.bottom_radius);
	//assert(mu >= -1.0 && mu <= 1.0);
	return (mu < 0.0 && r * r * (mu * mu - 1.0) + g_bottomRadius * g_bottomRadius >= 0.0);
}

vec2 GetRMuFromTransmittanceTextureUv(vec2 uv) {
	//assert(uv.x >= 0.0 && uv.x <= 1.0);
	//assert(uv.y >= 0.0 && uv.y <= 1.0);
	float r, mu;

	float x_r = uv.y;
	float x_mu = uv.x;
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

vec2 GetRMuSFromIrradianceTextureUv(vec2 uv) {
	//assert(uv.x >= 0.0 && uv.x <= 1.0);
	//assert(uv.y >= 0.0 && uv.y <= 1.0);
	float x_mu_s = GetUnitRangeFromTextureCoord(uv.x, 256);
	float x_r = GetUnitRangeFromTextureCoord(uv.y, 256);
	float r = g_bottomRadius + x_r * (g_topRadius - g_bottomRadius);
	float mu_s = ClampCosine(2.0 * x_mu_s - 1.0);
	return vec2(r, mu_s);
}

vec2 GetIrradianceTextureUvFromRMuS(float r, float mu_s) {
	//assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
	//assert(mu_s >= -1.0 && mu_s <= 1.0);
	float x_r = (r - g_bottomRadius) / (g_topRadius - g_bottomRadius);
	float x_mu_s = mu_s * 0.5 + 0.5;
	return vec2(GetTextureCoordFromUnitRange(x_mu_s, 256), GetTextureCoordFromUnitRange(x_r, 256));
}

vec4 GetScatteringTextureUvwzFromRMuMuSNu(float r, float mu, float mu_s, float nu, bool ray_r_mu_intersects_ground) 
{
	//assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
	//assert(mu >= -1.0 && mu <= 1.0);
	//assert(mu_s >= -1.0 && mu_s <= 1.0);
	//assert(nu >= -1.0 && nu <= 1.0);

	// Distance to top atmosphere boundary for a horizontal ray at ground level.
	float H = sqrt(g_topRadius * g_topRadius - g_bottomRadius * g_bottomRadius);
	// Distance to the horizon.
	float rho = sqrt(r * r - g_bottomRadius * g_bottomRadius);
	float u_r = rho / H;

	// Discriminant of the quadratic equation for the intersections of the ray
	// (r,mu) with the ground (see RayIntersectsGround).
	float r_mu = r * mu;
	float discriminant = r_mu * r_mu - r * r + g_bottomRadius * g_bottomRadius;
	float u_mu;
	if (ray_r_mu_intersects_ground) {
		// Distance to the ground for the ray (r,mu), and its minimum and maximum
		// values over all mu - obtained for (r,-1) and (r,mu_horizon).
		float d = -r_mu - sqrt(discriminant);
		float d_min = r - g_bottomRadius;
		float d_max = rho;
		u_mu = 0.5 - 0.5 * GetTextureCoordFromUnitRange(d_max == d_min ? 0.0 : (d - d_min) / (d_max - d_min), 128 / 2);
	}
	else {
		// Distance to the top atmosphere boundary for the ray (r,mu), and its
		// minimum and maximum values over all mu - obtained for (r,1) and
		// (r,mu_horizon).
		float d = -r_mu + sqrt(discriminant + H * H);
		float d_min = g_topRadius - r;
		float d_max = rho + H;
		u_mu = 0.5 + 0.5 * GetTextureCoordFromUnitRange((d - d_min) / (d_max - d_min), 128 / 2);
	}

	float d = DistanceToTopAtmosphereBoundary(g_bottomRadius, mu_s);
	float d_min = g_topRadius - g_bottomRadius;
	float d_max = H;
	float a = (d - d_min) / (d_max - d_min);
	float D = DistanceToTopAtmosphereBoundary(g_bottomRadius, g_mu_s_min);
	float A = (D - d_min) / (d_max - d_min);
	// An ad-hoc function equal to 0 for mu_s = mu_s_min (because then d = D and
	// thus a = A), equal to 1 for mu_s = 1 (because then d = d_min and thus
	// a = 0), and with a large slope around mu_s = 0, to get more texture 
	// samples near the horizon.
	float u_mu_s = GetTextureCoordFromUnitRange(max(1.0 - a / A, 0.0) / (1.0 + a), 32);

	float u_nu = (nu + 1.0) / 2.0;
	return vec4(u_nu, u_mu_s, u_mu, u_r);
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
	float rho = H * uvwz.w;
	r = sqrt(rho * rho + g_bottomRadius * g_bottomRadius);

	if (uvwz.z < 0.5) {
		// Distance to the ground for the ray (r,mu), and its minimum and maximum
		// values over all mu - obtained for (r,-1) and (r,mu_horizon) - from which
		// we can recover mu:
		float d_min = r - g_bottomRadius;
		float d_max = rho;
		float d = d_min + (d_max - d_min) * (2.0 * uvwz.z );
		mu = d == 0.0 ? float(-1.0) : ClampCosine(-(rho * rho + d * d) / (2.0 * r * d));
		ray_r_mu_intersects_ground = true;
	}
	else {
		// Distance to the top atmosphere boundary for the ray (r,mu), and its
		// minimum and maximum values over all mu - obtained for (r,1) and
		// (r,mu_horizon) - from which we can recover mu:
		float d_min = g_topRadius - r;
		float d_max = rho + H;
		float d = d_min + (d_max - d_min) * (2.0 * uvwz.z - 1.0);
		mu = d == 0.0 ? float(1.0) : ClampCosine((H * H - rho * rho - d * d) / (2.0 * r * d));
		ray_r_mu_intersects_ground = false;
	}

	float x_mu_s = uvwz.y;
	float d_min = g_topRadius - g_bottomRadius;
	float d_max = H;
	float D = DistanceToTopAtmosphereBoundary(g_bottomRadius, g_mu_s_min);
	float A = (D - d_min) / (d_max - d_min);
	float a = (A - x_mu_s * A) / (1.0 + x_mu_s * A);
	float d = d_min + min(a, A) * (d_max - d_min);
	mu_s = d == 0.0 ? float(1.0) : ClampCosine((H * H - d * d) / (2.0 * g_bottomRadius * d));

	nu = ClampCosine(uvwz.x * 2.0 - 1.0);

	if (ray_r_mu_intersects_ground)	
		r *= -1;

	return vec4(r, mu, mu_s, nu);
}

#endif