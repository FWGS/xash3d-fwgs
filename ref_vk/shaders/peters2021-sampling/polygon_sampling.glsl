// Copyright (C) 2021, Christoph Peters, Karlsruhe Institute of Technology
//
// This source code file is licensed under both the three-clause BSD license
// and the GPLv3. You may select, at your option, one of the two licenses. The
// corresponding license headers follow:
//
//
// Three-clause BSD license:
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
//  3. Neither the name of the copyright holder nor the names of its
//     contributors may be used to endorse or promote products derived from
//     this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//
//
// GPLv3:
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include "math_constants.glsl"


/*! This structure carries intermediate results that only need to be computed
	once per polygon and shading point to take samples proportional to solid
	angle. Sampling is performed by subdividing the convex polygon into
	triangles as triangle fan around vertex 0 and then using our variant of
	Arvo's method.*/
struct solid_angle_polygon_t {
	//! The number of vertices that form the polygon
	uint vertex_count;
	//! Normalized direction vectors from the shading point to each vertex
	vec3 vertex_dirs[MAX_POLYGON_VERTEX_COUNT];
	/*! A few intermediate quantities about the triangle consisting of vertices
		i + 1, 0, and i + 2. If the three vertices are v0, v1, v2, the entries
		are determinant(mat3(v0, v1, v2)), dot(v0 + v1, v2) and
		1.0f + dot(v0, v1).*/
	vec3 triangle_parameters[MAX_POLYGON_VERTEX_COUNT - 2];
	//! At index i, this array holds the solid angle of the triangle fan formed
	//! by vertices 0 to i + 2.
	float fan_solid_angles[MAX_POLYGON_VERTEX_COUNT - 2];
	//! The total solid angle of the polygon
	float solid_angle;
};


/*! A piecewise polynomial approximation to positive_atan(y). The maximal
	absolute error is 1.16e-05f. At least on Turing GPUs, it is faster but also
	significantly less accurate. The proper atan has at most 2 ulps of error
	there.*/
float fast_positive_atan(float y) {
	float rx;
	float ry;
	float rz;
	rx = (abs(y) > 1.0f) ? (1.0f / abs(y)) : abs(y);
	ry = rx * rx;
	rz = fma(ry, 0.02083509974181652f, -0.08513300120830536);
	rz = fma(ry, rz, 0.18014100193977356f);
	rz = fma(ry, rz, -0.3302994966506958f);
	ry = fma(ry, rz, 0.9998660087585449f);
	rz = fma(-2.0f * ry, rx, M_HALF_PI);
	rz = (abs(y) > 1.0f) ? rz : 0.0f;
	rx = fma(rx, ry, rz);
	return (y < 0.0f) ? (M_PI - rx) : rx;
}


/*! Returns an angle between 0 and M_PI such that tan(angle) == tangent. In
	other words, it is a version of atan() that is offset to be non-negative.
	Note that it may be switched to an approximate mode by the
	USE_BIASED_PROJECTED_SOLID_ANGLE_SAMPLING flag.*/
float positive_atan(float tangent) {
#ifdef USE_BIASED_PROJECTED_SOLID_ANGLE_SAMPLING
	return fast_positive_atan(tangent);
#else
	float offset = (tangent < 0.0f) ? M_PI : 0.0f;
	return atan(tangent) + offset;
#endif
}


/*! Prepares all intermediate values to sample a triangle fan around vertex 0
	(e.g. a convex polygon) proportional to solid angle using our method.
	\param vertex_count Number of vertices forming the polygon.
	\param vertices List of vertex locations.
	\param shading_position The location of the shading point.
	\return Input for sample_solid_angle_polygon().*/
solid_angle_polygon_t prepare_solid_angle_polygon_sampling(uint vertex_count, vec3 vertices[MAX_POLYGON_VERTEX_COUNT], vec3 shading_position) {
	solid_angle_polygon_t polygon;
	polygon.vertex_count = vertex_count;
	// Normalize vertex directions
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
		polygon.vertex_dirs[i] = normalize(vertices[i] - shading_position);
	}
	// Prepare a Householder transform that maps vertex 0 onto (+/-1, 0, 0). We
	// only store the yz-components of that Householder vector and a factor of
	// 2.0f / sqrt(abs(polygon.vertex_dirs[0].x) + 1.0f) is pulled in there to
	// save on multiplications later. This approach is necessary to avoid
	// numerical instabilities in determinant computation below.
	float householder_sign = (polygon.vertex_dirs[0].x > 0.0f) ? -1.0f : 1.0f;
	vec2 householder_yz = polygon.vertex_dirs[0].yz * (1.0f / (abs(polygon.vertex_dirs[0].x) + 1.0f));
	// Compute solid angles and prepare sampling
	polygon.solid_angle = 0.0f;
	float previous_dot_1_2 = dot(polygon.vertex_dirs[0], polygon.vertex_dirs[1]);
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 2; ++i) {
		if (i >= 1 && i + 2 >= vertex_count) break;
		// We look at one triangle of the triangle fan at a time
		vec3 vertices[3] = {
			polygon.vertex_dirs[i + 1],
			polygon.vertex_dirs[0],
			polygon.vertex_dirs[i + 2]};
		float dot_0_1 = previous_dot_1_2;
		float dot_0_2 = dot(vertices[0], vertices[2]);
		float dot_1_2 = dot(vertices[1], vertices[2]);
		previous_dot_1_2 = dot_1_2;
		// Compute the bottom right minor of vertices after application of the
		// Householder transform
		float dot_householder_0 = fma(-householder_sign, vertices[0].x, dot_0_1);
		float dot_householder_2 = fma(-householder_sign, vertices[2].x, dot_1_2);
		mat2 bottom_right_minor = mat2(
			fma(vec2(-dot_householder_0), householder_yz, vertices[0].yz),
			fma(vec2(-dot_householder_2), householder_yz, vertices[2].yz));
		// The absolute value of the determinant of vertices equals the 2x2
		// determinant because the Householder transform turns the first column
		// into (+/-1, 0, 0)
		float simplex_volume = abs(determinant(bottom_right_minor));
		// Compute the solid angle of the triangle using a formula proposed by:
		// A. Van Oosterom and J. Strackee, 1983, The Solid Angle of a
		// Plane Triangle, IEEE Transactions on Biomedical Engineering 30:2
		// https://doi.org/10.1109/TBME.1983.325207
		float dot_0_2_plus_1_2 = dot_0_2 + dot_1_2;
		float one_plus_dot_0_1 = 1.0f + dot_0_1;
		float tangent = simplex_volume / (one_plus_dot_0_1 + dot_0_2_plus_1_2);
		float triangle_solid_angle = 2.0f * positive_atan(tangent);
		polygon.solid_angle += triangle_solid_angle;
		polygon.fan_solid_angles[i] = polygon.solid_angle;
		// Some intermediate results from above help us with sampling
		polygon.triangle_parameters[i] = vec3(simplex_volume, dot_0_2_plus_1_2, one_plus_dot_0_1);
	}
	return polygon;
}


/*! An implementation of mix() using two fused-multiply add instructions. Used
	because the native mix() implementation had stability issues in a few
	spots. Credit to Fabian Giessen's blog, see:
	https://fgiesen.wordpress.com/2012/08/15/linear-interpolation-past-present-and-future/
	*/
float mix_fma(float x, float y, float a) {
	return fma(a, y, fma(-a, x, x));
}


/*! Given the output of prepare_solid_angle_polygon_sampling(), this function
	maps the given random numbers in the range from 0 to 1 to a normalized
	direction vector providing a sample of the solid angle of the polygon in
	the original space (used for arguments of
	prepare_solid_angle_polygon_sampling()). Samples are distributed in
	proportion to solid angle assuming uniform inputs.*/
vec3 sample_solid_angle_polygon(solid_angle_polygon_t polygon, vec2 random_numbers) {
	// Decide which triangle needs to be sampled
	float target_solid_angle = polygon.solid_angle * random_numbers[0];
	float subtriangle_solid_angle = target_solid_angle;
	vec3 parameters = polygon.triangle_parameters[0];
	vec3 vertices[3] = {
		polygon.vertex_dirs[1], polygon.vertex_dirs[0], polygon.vertex_dirs[2]
	};
	[[unroll]]
	for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 3; ++i) {
		if (i + 3 >= polygon.vertex_count || polygon.fan_solid_angles[i] >= target_solid_angle) break;
		subtriangle_solid_angle = target_solid_angle - polygon.fan_solid_angles[i];
		vertices[0] = polygon.vertex_dirs[i + 2];
		vertices[2] = polygon.vertex_dirs[i + 3];
		parameters = polygon.triangle_parameters[i + 1];
	}
	// Construct a new vertex 2 on the arc between vertices 0 and 2 such that
	// the resulting triangle has solid angle subtriangle_solid_angle
	vec2 cos_sin = vec2(cos(0.5f * subtriangle_solid_angle), sin(0.5f * subtriangle_solid_angle));
	vec3 offset = vertices[0] * (parameters[0] * cos_sin.x - parameters[1] * cos_sin.y) + vertices[2] * (parameters[2] * cos_sin.y);
	vec3 new_vertex_2 = fma(2.0f * vec3(dot(vertices[0], offset) / dot(offset, offset)), offset, -vertices[0]);
	// Now sample the line between vertex 1 and the newly created vertex 2
	float s2 = dot(vertices[1], new_vertex_2);
	float s = mix_fma(1.0f, s2, random_numbers[1]);
	float denominator = fma(-s2, s2, 1.0f);
	float t_normed = sqrt(fma(-s, s, 1.0f) / denominator);
	// s2 may exceed one due to rounding error. random_numbers[1] is the
	// limit of t_normed for s2 -> 1.
	t_normed = (denominator > 0.0f) ? t_normed : random_numbers[1];
	return fma(-t_normed, s2, s) * vertices[1] + t_normed * new_vertex_2;
}


/*! This structure carries intermediate results that only need to be computed
	once per polygon and shading point to take samples proportional to
	projected solid angle.*/
struct projected_solid_angle_polygon_t {
	//! The number of vertices that form the polygon
	uint vertex_count;
	/*! The x- and y-coordinates of each polygon vertex in a coordinate system
		where the normal is the z-axis. The vertices are sorted
		counterclockwise.*/
	vec2 vertices[MAX_POLYGON_VERTEX_COUNT];
	/*! For each vertex in vertices, this vector describes the ellipse for the
		next edge in counterclockwise direction. The last entry is meaningless,
		except in the central case. For vertex 0, it holds the outer ellipse.
		\see ellipse_from_edge() */
	vec2 ellipses[MAX_POLYGON_VERTEX_COUNT];
	//! The inner ellipse adjacent to vertex 0. If the x-component is positive,
	//! the central case is present.
	vec2 inner_ellipse_0;
	/*! At index i, this array holds the projected solid angle of the polygon
		in the sector between (sorted) vertices i and (i + 1) % vertex_count
		In the central case, entry vertex_count - 1 is meaningful, otherwise
		not.*/
	float sector_projected_solid_angles[MAX_POLYGON_VERTEX_COUNT];
	//! The total projected solid angle of the polygon
	float projected_solid_angle;
};


//! Computes a * b - c * d with at most 1.5 ulps of error in the result. See
//! https://pharr.org/matt/blog/2019/11/03/difference-of-floats.html or
//! Claude-Pierre Jeannerod, Nicolas Louvet and Jean-Michel Muller, 2013,
//! Further analysis of Kahan's algorithm for the accurate computation of 2x2
//! determinants, AMS Mathematics of Computation 82:284,
//! https://doi.org/10.1090/S0025-5718-2013-02679-8
float kahan(float a, float b, float c, float d) {
	// Uncomment the line below to improve efficiency but reduce accuracy
	// return a * b - c * d;
	float cd = c * d;
	float error = fma(c, d, -cd);
	float result = fma(a, b, -cd);
	return result - error;
}


//! Implements a cross product using Kahan's algorithm for every single entry,
//! i.e. the error in each output entry is at most 1.5 ulps
vec3 cross_stable(vec3 lhs, vec3 rhs) {
	return vec3(
		kahan(lhs.y, rhs.z, lhs.z, rhs.y),
		kahan(lhs.z, rhs.x, lhs.x, rhs.z),
		kahan(lhs.x, rhs.y, lhs.y, rhs.x)
	);
}


//! \return The given vector, rotated 90 degrees counterclockwise around the
//! 		origin
vec2 rotate_90(vec2 input_vector) {
	return vec2(-input_vector.y, input_vector.x);
}


//! \return true iff the given ellipse is marked as inner ellipse, i.e. iff the
//!		spherical polygon that is bounded by it is further away from the zenith
//!		than this ellipse.
bool is_inner_ellipse(vec2 ellipse) {
	// If the implementation below causes you trouble, e.g. because you want to
	// port to a different language, you may replace it by the commented line
	// below but it will lead to seldom artifacts when ellipse.x == 0.0f.
	// return ellipse.x < 0.0f;
	// Extract the sign bit from ellipse.x (to be able to tell apart +0 and -0)
	return (floatBitsToUint(ellipse.x) & 0x80000000) != 0;
}


//! \return true iff the given polygon contains the zenith (also known as
//!		normal vector).
bool is_central_case(projected_solid_angle_polygon_t polygon) {
	return polygon.inner_ellipse_0.x > 0.0f;
}


/*! Takes the great circle for the plane through the origin and the given two
	points and constructs an ellipse for its projection to the xy-plane.
	\return A vector ellipse such that a point is on the ellipse if and only if
		dot(ellipse, point) * dot(ellipse, point) + dot(point, point) == 1.0f.
		In other words, it is a normal vector of the great circle in half-
		vector space. The sign bit of x encodes whether the edge runs clockwise
		from vertex_0 to vertex_1 (inner ellipse) or not.
	\see is_inner_ellipse() */
vec2 ellipse_from_edge(vec3 vertex_0, vec3 vertex_1) {
	vec3 normal = cross_stable(vertex_0, vertex_1);
	float scaling = 1.0f / normal.z;
	scaling = is_inner_ellipse(normal.xy) ? -scaling : scaling;
	vec2 ellipse = normal.xy * scaling;
	// By convention, degenerate ellipses are outer ellipses, i.e. the first
	// component is infinite
	ellipse.x = (normal.z != 0.0f) ? ellipse.x : M_INFINITY;
	return ellipse;
}


//! Transforms the given point using the matrix that characterizes the given
//! ellipse (as produced by ellipse_from_edge()). To be precise, this matrix is
//! identity + outerProduct(ellipse, ellipse).
vec2 ellipse_transform(vec2 ellipse, vec2 point) {
	return fma(vec2(dot(ellipse, point)), ellipse, point);
}


//! Given an ellipse in the format produced by ellipse_from_edge(), this
//! function returns the determinant of the matrix characterizing this
//! ellipse.
float get_ellipse_det(vec2 ellipse) {
	return fma(ellipse.x, ellipse.x, fma(ellipse.y, ellipse.y, 1.0f));
}

//! Returns the reciprocal square root of the ellipse determinant produced by
//! get_ellipse_det().
float get_ellipse_rsqrt_det(vec2 ellipse) {
	return inversesqrt(get_ellipse_det(ellipse));
}

//! \return Reciprocal square of get_ellipse_direction_factor(ellipse, dir)
float get_ellipse_direction_factor_rsq(vec2 ellipse, vec2 dir) {
	float ellipse_dot_dir = dot(ellipse, dir);
	float dir_dot_dir = dot(dir, dir);
	return fma(ellipse_dot_dir, ellipse_dot_dir, dir_dot_dir);
}

/*! Computes a factor by which a direction vector has to be multiplied to
	obtain a point on the given ellipse.
	\param ellipse An ellipse as produced by ellipse_from_edge().
	\param dir The direction vector to be scaled onto the ellipse.
	\return get_ellipse_direction_factor(ellipse, dir) * dir is a point on
		the ellipse.*/
float get_ellipse_direction_factor(vec2 ellipse, vec2 dir) {
	return inversesqrt(get_ellipse_direction_factor_rsq(ellipse, dir));
}

//! Like get_ellipse_direction_factor() but assumes that the given direction is
//! normalized. Faster.
float get_ellipse_normalized_direction_factor(vec2 ellipse, vec2 normalized_dir) {
	float ellipse_dot_dir = dot(ellipse, normalized_dir);
	return inversesqrt(fma(ellipse_dot_dir, ellipse_dot_dir, 1.0f));
}


//! Helper for get_area_between_ellipses_in_sector() and
//! sample_sector_between_ellipses()
float get_area_between_ellipses_in_sector_from_tangents(float inner_rsqrt_det, float inner_tangent, float outer_rsqrt_det, float outer_tangent) {
	float inner_area = inner_rsqrt_det * positive_atan(inner_tangent);
	float result = fma(outer_rsqrt_det, positive_atan(outer_tangent), -inner_area);
	// Sort out NaNs and negative results
	return (result > 0.0f) ? (0.5f * result) : 0.0f;
}


/*! Returns the signed area between the given outer and inner ellipses within
	the sector enclosed by dir_0 and dir_1. Besides ellipses as produced by
	ellipse_from_edge(), you also have to pass output of
	get_ellipse_rsqrt_det(). Faster than calling get_ellipse_area_in_sector()
	twice.*/
float get_area_between_ellipses_in_sector(vec2 inner_ellipse, float inner_rsqrt_det, vec2 outer_ellipse, float outer_rsqrt_det, vec2 dir_0, vec2 dir_1) {
	float det_dirs = max(+0.0f, dot(dir_1, rotate_90(dir_0)));
	float inner_dot = inner_rsqrt_det * dot(dir_0, ellipse_transform(inner_ellipse, dir_1));
	float outer_dot = outer_rsqrt_det * dot(dir_0, ellipse_transform(outer_ellipse, dir_1));
	return get_area_between_ellipses_in_sector_from_tangents(
		inner_rsqrt_det, det_dirs / inner_dot,
		outer_rsqrt_det, det_dirs / outer_dot);
}


/*! Computes the area for the intersection of the given ellipse and the sector
	between the given two directions (going counterclockwise from dir_0 to
	dir_1 for at most 180 degrees). The scaling of the directions is
	irrelevant.
	\see ellipse_from_edge() */
float get_ellipse_area_in_sector(vec2 ellipse, vec2 dir_0, vec2 dir_1) {
	float ellipse_rsqrt_det = get_ellipse_rsqrt_det(ellipse);
	float det_dirs = max(+0.0f, dot(dir_1, rotate_90(dir_0)));
	float ellipse_dot = ellipse_rsqrt_det * dot(dir_0, ellipse_transform(ellipse, dir_1));
	float area = 0.5f * ellipse_rsqrt_det * positive_atan(det_dirs / ellipse_dot);
	// For degenerate ellipses, the result may be NaN but must be 0.0f
	return (ellipse_rsqrt_det > 0.0f) ? area : 0.0f;
}


/*! Swaps vertices lhs and rhs (along with corresponding ellipses) of the given
	polygon if the shorter path from lhs to rhs is clockwise. If the vertices
	have identical directions in the xy-plane, vertices with degenerate
	ellipses come first.
	\note To avoid costly register spilling, lhs and rhs must be compile time
		constants.*/
void compare_and_swap(inout projected_solid_angle_polygon_t polygon, uint lhs, uint rhs) {
	vec2 lhs_copy = polygon.vertices[lhs];
	// This line is designed to agree with the implementation of cross_stable
	// for the z-coordinate, which determines if ellipses are inner or outer
	float normal_z = kahan(lhs_copy.x, -polygon.vertices[rhs].y, lhs_copy.y, -polygon.vertices[rhs].x);
	// Tie breaker: If both vertices are at the same angle (i.e. on a common
	// great circle through the zenith), the one with the degenerate ellipse
	// comes first
	bool swap = (normal_z == 0.0f) ? isinf(polygon.ellipses[rhs].x) : (normal_z > 0.0f);
	polygon.vertices[lhs] = swap ? polygon.vertices[rhs] : lhs_copy;
	polygon.vertices[rhs] = swap ? lhs_copy : polygon.vertices[rhs];
	lhs_copy = polygon.ellipses[lhs];
	polygon.ellipses[lhs] = swap ? polygon.ellipses[rhs] : lhs_copy;
	polygon.ellipses[rhs] = swap ? lhs_copy : polygon.ellipses[rhs];
}


//! Sorts the vertices of the given convex polygon counterclockwise using a
//! special sorting network. For non-convex polygons, the method may fail.
void sort_convex_polygon_vertices(inout projected_solid_angle_polygon_t polygon) {
	if (polygon.vertex_count == 3) {
		compare_and_swap(polygon, 1, 2);
	}
#if MAX_POLYGON_VERTEX_COUNT >= 4
	else if (polygon.vertex_count == 4) {
		compare_and_swap(polygon, 1, 3);
	}
#endif
#if MAX_POLYGON_VERTEX_COUNT >= 5
	else if (polygon.vertex_count == 5) {
		compare_and_swap(polygon, 2, 4);
		compare_and_swap(polygon, 1, 3);
		compare_and_swap(polygon, 1, 2);
		compare_and_swap(polygon, 0, 3);
		compare_and_swap(polygon, 3, 4);
	}
#endif
#if MAX_POLYGON_VERTEX_COUNT >= 6
	else if (polygon.vertex_count == 6) {
		compare_and_swap(polygon, 3, 5);
		compare_and_swap(polygon, 2, 4);
		compare_and_swap(polygon, 1, 5);
		compare_and_swap(polygon, 0, 4);
		compare_and_swap(polygon, 4, 5);
		compare_and_swap(polygon, 1, 3);
	}
#endif
#if MAX_POLYGON_VERTEX_COUNT >= 7
	else if (polygon.vertex_count == 7) {
		compare_and_swap(polygon, 2, 5);
		compare_and_swap(polygon, 1, 6);
		compare_and_swap(polygon, 5, 6);
		compare_and_swap(polygon, 3, 4);
		compare_and_swap(polygon, 0, 4);
		compare_and_swap(polygon, 4, 6);
		compare_and_swap(polygon, 1, 3);
		compare_and_swap(polygon, 3, 5);
		compare_and_swap(polygon, 4, 5);
	}
#endif
#if MAX_POLYGON_VERTEX_COUNT >= 8
	else if (polygon.vertex_count == 8) {
		compare_and_swap(polygon, 2, 6);
		compare_and_swap(polygon, 3, 7);
		compare_and_swap(polygon, 1, 5);
		compare_and_swap(polygon, 0, 4);
		compare_and_swap(polygon, 4, 6);
		compare_and_swap(polygon, 5, 7);
		compare_and_swap(polygon, 6, 7);
		compare_and_swap(polygon, 4, 5);
		compare_and_swap(polygon, 1, 3);
	}
#endif
	// This comparison is shared by all sorting networks
	compare_and_swap(polygon, 0, 2);
#if MAX_POLYGON_VERTEX_COUNT >= 4
	if (polygon.vertex_count >= 4) {
		// This comparison is shared by all sorting networks except the one for
		// triangles
		compare_and_swap(polygon, 2, 3);
	}
#endif
	// This comparison is shared by all sorting networks
	compare_and_swap(polygon, 0, 1);
}


/*! Prepares all intermediate values to sample a convex polygon proportional to
	projected solid angle.
	\param vertex_count Number of vertices forming the polygon (at least 3).
	\param vertices List of vertex locations in a coordinate system where the
		shading position is the origin and the normal is the z-axis. The
		polygon should be already clipped against the plane z=0. If
		vertex_count < MAX_POLYGON_VERTEX_COUNT, the first vertex has to be
		repeated at vertex_count. They need not be normalized but if you
		encounter issues with under- or overflow (e.g. NaN or INF outputs),
		normalization may help. The polygon must be convex, and the winding of
		the vertices as seen from the origin must be clockwise. No three
		vertices should be collinear.
	\return Intermediate values for sampling.*/
projected_solid_angle_polygon_t prepare_projected_solid_angle_polygon_sampling(uint vertex_count, vec3 vertices[MAX_POLYGON_VERTEX_COUNT]) {
	projected_solid_angle_polygon_t polygon;
	// Copy vertices and assign ellipses
	polygon.vertex_count = vertex_count;
	polygon.inner_ellipse_0 = vec2(1.0f, 0.0f);
	polygon.vertices[0] = vertices[0].xy;
	polygon.ellipses[0] = ellipse_from_edge(vertices[0], vertices[1]);
	vec2 previous_ellipse = polygon.ellipses[0];
	[[unroll]]
	for (uint i = 1; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
		polygon.vertices[i] = vertices[i].xy;
		if (i > 2 && i == polygon.vertex_count) break;
		vec2 ellipse = ellipse_from_edge(vertices[i], vertices[(i + 1) % MAX_POLYGON_VERTEX_COUNT]);
		bool ellipse_inner = is_inner_ellipse(ellipse);
		// If the edge is an inner edge, the order is going to flip
		polygon.ellipses[i] = ellipse_inner ? previous_ellipse : ellipse;
		// In doing so, we drop one ellipse, unless we store it explicitly
		polygon.inner_ellipse_0 = (is_inner_ellipse(previous_ellipse) && !ellipse_inner) ? previous_ellipse : polygon.inner_ellipse_0;
		previous_ellipse = ellipse;
	}
	// Same thing for the first vertex (i.e. here we close the loop)
	vec2 ellipse = polygon.ellipses[0];
	bool ellipse_inner = is_inner_ellipse(ellipse);
	polygon.ellipses[0] = ellipse_inner ? previous_ellipse : ellipse;
	polygon.inner_ellipse_0 = (is_inner_ellipse(previous_ellipse) && !ellipse_inner) ? previous_ellipse : polygon.inner_ellipse_0;
	// Compute projected solid angles per sector and in total
	polygon.projected_solid_angle = 0.0f;
	if (is_central_case(polygon)) {
		// In the central case, we have polygon.vertex_count sectors, each
		// bounded by a single ellipse
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
			if (i > 2 && i == polygon.vertex_count) break;
			polygon.sector_projected_solid_angles[i] = get_ellipse_area_in_sector(polygon.ellipses[i], polygon.vertices[i], polygon.vertices[(i + 1) % MAX_POLYGON_VERTEX_COUNT]);
			polygon.projected_solid_angle += polygon.sector_projected_solid_angles[i];
		}
	}
	else {
		// Sort vertices counter clockwise
		sort_convex_polygon_vertices(polygon);
		// There are polygon.vertex_count - 1 sectors, each bounded by an inner
		// and an outer ellipse
		vec2 inner_ellipse = polygon.inner_ellipse_0;
		float inner_rsqrt_det = get_ellipse_rsqrt_det(inner_ellipse);
		vec2 outer_ellipse;
		float outer_rsqrt_det;
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 1; ++i) {
			if (i > 1 && i + 1 == polygon.vertex_count) break;
			vec2 vertex_ellipse = polygon.ellipses[i];
			bool vertex_inner = is_inner_ellipse(vertex_ellipse);
			float vertex_rsqrt_det = get_ellipse_rsqrt_det(vertex_ellipse);
			if (i == 0) {
				outer_ellipse = vertex_ellipse;
				outer_rsqrt_det = vertex_rsqrt_det;
			}
			else {
				inner_ellipse = vertex_inner ? vertex_ellipse : inner_ellipse;
				inner_rsqrt_det = vertex_inner ? vertex_rsqrt_det : inner_rsqrt_det;
				outer_ellipse = vertex_inner ? outer_ellipse : vertex_ellipse;
				outer_rsqrt_det = vertex_inner ? outer_rsqrt_det : vertex_rsqrt_det;
			}
			polygon.sector_projected_solid_angles[i] = get_area_between_ellipses_in_sector(
				inner_ellipse, inner_rsqrt_det, outer_ellipse, outer_rsqrt_det, polygon.vertices[i], polygon.vertices[i + 1]);
			polygon.projected_solid_angle += polygon.sector_projected_solid_angles[i];
		}
	}
	return polygon;
}


/*! \return A scalar multiple of rhs that is not too far from being normalized.
		For the result, length() returns something between sqrt(2.0f) and 8.0f.
		The sign gets flipped such that the dot product of semi_circle and the
		result is non-negative.
	\note Introduces less latency than normalize() and does not use special
		functions. Useful to avoid under- and overflow when working with
		homogeneous coordinates. The result is undefined if rhs is zero.*/
vec2 normalize_approx_and_flip(vec2 rhs, vec2 semi_circle) {
	float scaling = abs(rhs.x) + abs(rhs.y);
	// By flipping each bit on the exponent E, we turn it into 1 - E, which is
	// close enough to a reciprocal.
	scaling = uintBitsToFloat(floatBitsToUint(scaling) ^ 0x7F800000u);
	// If the line above causes you any sort of trouble (e.g. because you want
	// to port the code to another language or you are doing differentiable
	// rendering), just use this one instead:
	// scaling = 1.0f / scaling;
	// Flip the sign as needed
	scaling = (dot(rhs, semi_circle) >= 0.0f) ? scaling : -scaling;
	return scaling * rhs;
}


/*! Returns a solution to the given homogeneous quadratic equation, i.e. a
	non-zero vector root such that dot(root, quadratic * root) == 0.0f. The
	returned root depends continuously on quadratic. Pass -quadratic if you
	want the other root.
	\note The implementation is as proposed by Blinn, except that we do not
	have a special case for quadratic[0][1] + quadratic[1][0] == 0.0f. Unlike
	the standard quadratic formula, it allows us to postpone a division and is
	stable in all cases.
	James F. Blinn 2006, How to Solve a Quadratic Equation, Part 2, IEEE
	Computer Graphics and Applications 26:2 https://doi.org/10.1109/MCG.2006.35
*/
vec2 solve_homogeneous_quadratic(mat2 quadratic) {
	float coeff_xy = 0.5f * (quadratic[0][1] + quadratic[1][0]);
	float sqrt_discriminant = sqrt(max(0.0f, coeff_xy * coeff_xy - quadratic[0][0] * quadratic[1][1]));
	float scaled_root = abs(coeff_xy) + sqrt_discriminant;
	return (coeff_xy >= 0.0f) ? vec2(scaled_root, -quadratic[0][0]) : vec2(quadratic[1][1], scaled_root);
}


/*! Generates a sample between two ellipses and in a specified sector. The
	sample is distributed uniformly with respect to the area measure.
	\param random_numbers A pair of independent uniform random numbers on [0,1]
	\param target_area random_numbers[0] multiplied by the projected solid
		angle of the area to be sampled.
	\param inner_ellipse, outer_ellipse The inner and outer ellipse, as
		 produced by ellipse_from_edge().
	\param dir_0, dir_1 Two direction vectors bounding the sector. They need
		not be normalized.
	\param iteration_count The number of iterations to perform. Lower values
		trade speed for bias. Two iterations give practically no bias.
	\return The sample in Cartesian coordinates.*/
vec2 sample_sector_between_ellipses(vec2 random_numbers, float target_area, vec2 inner_ellipse, vec2 outer_ellipse, vec2 dir_0, vec2 dir_1, uint iteration_count) {
	// For the initialization, split the sector in half
	vec2 quad_dirs[3];
	quad_dirs[0] = normalize(dir_0);
	quad_dirs[2] = normalize(dir_1);
	quad_dirs[1] = quad_dirs[0] + quad_dirs[2];
	// Compute where these lines intersect the ellipses. The six intersection
	// points define two adjacent quads.
	float normalization_factor[2][3] = {
		{
			get_ellipse_normalized_direction_factor(inner_ellipse, quad_dirs[0]),
			get_ellipse_direction_factor(inner_ellipse, quad_dirs[1]),
			get_ellipse_normalized_direction_factor(inner_ellipse, quad_dirs[2])
		},
		{
			get_ellipse_normalized_direction_factor(outer_ellipse, quad_dirs[0]),
			get_ellipse_direction_factor(outer_ellipse, quad_dirs[1]),
			get_ellipse_normalized_direction_factor(outer_ellipse, quad_dirs[2])
		}
	};
	// Compute the relative size of the areas inside these quads
	float sector_areas[2] = {
		normalization_factor[1][0] * normalization_factor[1][1] - normalization_factor[0][0] * normalization_factor[0][1],
		normalization_factor[1][1] * normalization_factor[1][2] - normalization_factor[0][1] * normalization_factor[0][2]
	};
	// Now pick which of the two quads should be sampled for the
	// initialization. If it is not the second, we move data such that the
	// relevant array indices are 1 and 2 anyway.
	float target_quad_area = mix_fma(-sector_areas[0], sector_areas[1], random_numbers[0]);
	quad_dirs[2] = (target_quad_area <= 0.0f) ? quad_dirs[0] : quad_dirs[2];
	normalization_factor[0][2] = (target_quad_area <= 0.0f) ? normalization_factor[0][0] : normalization_factor[0][2];
	normalization_factor[1][2] = (target_quad_area <= 0.0f) ? normalization_factor[1][0] : normalization_factor[1][2];
	target_quad_area += (target_quad_area <= 0.0f) ? sector_areas[0] : -sector_areas[1];
	// We have been a bit lazy about area computation before but now we need
	// all the factors (except for a factor of 0.5 that cancels with a 2 later)
	target_quad_area *= abs(determinant(mat2(quad_dirs[1], quad_dirs[2])));
	// Construct normal vectors for the inner and outer edge of the selected
	// quad. We construct the normal like a half vector (i.e. by addition)
	// because it is less prone to cancellation than an approach using the edge
	// direction (i.e. subtraction of sometimes nearly identical vectors)
	vec2 quad_normals[2] = {
		quad_dirs[1] * normalization_factor[0][1] + quad_dirs[2] * normalization_factor[0][2],
		quad_dirs[1] * normalization_factor[1][1] + quad_dirs[2] * normalization_factor[1][2]
	};
	quad_normals[0] = ellipse_transform(inner_ellipse, quad_normals[0]);
	quad_normals[1] = ellipse_transform(outer_ellipse, quad_normals[1]);
	// Construct complete line equations
	float quad_offsets[2] = {
		dot(quad_normals[0], quad_dirs[1]) * normalization_factor[0][1],
		dot(quad_normals[1], quad_dirs[1]) * normalization_factor[1][1]
	};
	// Now sample the direction within the selected quad by constructing a
	// quadratic equation. This is the initialization for the iteration.
	mat2 quadratic = outerProduct((quad_offsets[1] * normalization_factor[1][2]) * rotate_90(quad_dirs[2]), quad_normals[0]);
	quadratic -= outerProduct((quad_offsets[0] * normalization_factor[0][2]) * rotate_90(quad_dirs[2]) + target_quad_area * quad_normals[0], quad_normals[1]);
	vec2 current_dir = solve_homogeneous_quadratic(quadratic);

#ifndef USE_BIASED_PROJECTED_SOLID_ANGLE_SAMPLING
	// For boundary values, the initialization is perfect but the iteration may
	// be unstable, so we disable it
	float acceptable_error = 1.0e-5f;
	iteration_count = (abs(random_numbers.x - 0.5f) <= 0.5f - acceptable_error) ? iteration_count : 0;

	// Now refine this initialization iteratively
	float inner_rsqrt_det = get_ellipse_rsqrt_det(inner_ellipse);
	float outer_rsqrt_det = get_ellipse_rsqrt_det(outer_ellipse);
	[[dont_unroll]]
	for (uint i = 0; i != iteration_count; ++i) {
		// Avoid under- or overflow and flip the sign so that the clamping to
		// zero below makes sense
		current_dir = normalize_approx_and_flip(current_dir, quad_dirs[1]);
		// Transform current_dir using both ellipses
		vec2 inner_dir = ellipse_transform(inner_ellipse, current_dir);
		vec2 outer_dir = ellipse_transform(outer_ellipse, current_dir);
		// Evaluate the objective function (reusing inner_dir and outer_dir)
		float det_dirs = max(+0.0f, dot(current_dir, rotate_90(quad_dirs[0])));
		float error = target_area - get_area_between_ellipses_in_sector_from_tangents(
			inner_rsqrt_det, det_dirs / (inner_rsqrt_det * dot(quad_dirs[0], inner_dir)),
			outer_rsqrt_det, det_dirs / (outer_rsqrt_det * dot(quad_dirs[0], outer_dir)));
		// Construct a homogeneous quadratic whose solutions include the next
		// step of the iteration
		quadratic = outerProduct(inner_dir - outer_dir, rotate_90(current_dir)) - outerProduct((2.0f * error) * inner_dir, outer_dir);
		current_dir = solve_homogeneous_quadratic(quadratic);
	}
#endif

	// The halved sector is at most 90 degrees large, so the dot product with
	// the half vector has to be positive
	current_dir = (dot(current_dir, quad_dirs[1]) >= 0.0f) ? current_dir : -current_dir;
	// Sample a squared radius uniformly between the two ellipses
	float inner_factor = 1.0f / get_ellipse_direction_factor_rsq(inner_ellipse, current_dir);
	float outer_factor = 1.0f / get_ellipse_direction_factor_rsq(outer_ellipse, current_dir);
	current_dir *= sqrt(mix_fma(inner_factor, outer_factor, random_numbers[1]));
	return current_dir;
}


/*! Produces a sample in the solid angle of the given polygon. If the random 
	numbers are uniform in [0,1]^2, the sample is uniform in the projected
	solid angle of the polygon.
	\param polygon Output of prepare_projected_solid_angle_polygon_sampling().
	\param random_numbers A uniform point in [0,1]^2.
	\return A sample on the upper hemisphere (i.e. z>=0) in Cartesian
		coordinates.*/
vec3 sample_projected_solid_angle_polygon(projected_solid_angle_polygon_t polygon, vec2 random_numbers) {
	float target_projected_solid_angle = random_numbers[0] * polygon.projected_solid_angle;
	// Distinguish between the central case
	vec3 sampled_dir;
	vec2 outer_ellipse;
	vec2 dir_0;
	if (is_central_case(polygon)) {
		// Select a sector and copy the relevant attributes
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT; ++i) {
			if (i > 0) {
				target_projected_solid_angle -= polygon.sector_projected_solid_angles[i - 1];
			}
			outer_ellipse = polygon.ellipses[i];
			dir_0 = polygon.vertices[i];
			if ((i >= 2 && i + 1 == polygon.vertex_count) || target_projected_solid_angle < polygon.sector_projected_solid_angles[i])
				break;
		}
		// Sample a direction within the sector
		float sqrt_det = sqrt(get_ellipse_det(outer_ellipse));
		float angle = 2.0f * target_projected_solid_angle * sqrt_det;
		sampled_dir.xy = (cos(angle) * sqrt_det) * dir_0 + sin(angle) * rotate_90(ellipse_transform(outer_ellipse, dir_0));
		// Sample a squared radius uniformly within the ellipse
		sampled_dir.xy *= sqrt(random_numbers[1] / get_ellipse_direction_factor_rsq(outer_ellipse, sampled_dir.xy));
	}
	// And the decentral case
	else {
		// Select a sector and copy the relevant attributes
		float sector_projected_solid_angle;
		vec2 inner_ellipse = polygon.inner_ellipse_0;
		vec2 dir_1;
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 1; ++i) {
			vec2 vertex_ellipse = polygon.ellipses[i];
			if (i == 0) {
				outer_ellipse = vertex_ellipse;
			}
			else {
				target_projected_solid_angle -= polygon.sector_projected_solid_angles[i - 1];
				bool vertex_inner = is_inner_ellipse(vertex_ellipse);
				inner_ellipse = vertex_inner ? vertex_ellipse : inner_ellipse;
				outer_ellipse = vertex_inner ? outer_ellipse : vertex_ellipse;
			}
			dir_0 = polygon.vertices[i];
			dir_1 = polygon.vertices[i + 1];
			sector_projected_solid_angle = polygon.sector_projected_solid_angles[i];
			if ((i >= 1 && i + 2 == polygon.vertex_count) || target_projected_solid_angle < sector_projected_solid_angle)
				break;
		}
		// Sample it
		random_numbers[0] = target_projected_solid_angle / sector_projected_solid_angle;
		sampled_dir.xy = sample_sector_between_ellipses(random_numbers, target_projected_solid_angle, inner_ellipse, outer_ellipse, dir_0, dir_1, 2);
	}
	// Construct the sample
	sampled_dir.z = sqrt(max(0.0f, fma(-sampled_dir.x, sampled_dir.x, fma(-sampled_dir.y, sampled_dir.y, 1.0f))));
	return sampled_dir;
}


/*! Determines the error of a sample from the projected solid angle of a
	polygon due to the iterative procedure.
	\param polygon Output of prepare_projected_solid_angle_polygon_sampling().
	\param random_numbers The value passed for random_numbers in
		sample_projected_solid_angle_polygon().
	\param sampled_dir The direction returned by
		sample_projected_solid_angle_polygon().
	\return The first component is the signed backward error: The difference
		between random_number_0 and the random number that would yield
		sampled_dir with perfect computation. The second component is the
		backward error, multiplied by the projected solid angle of the polygon.
		The third component is the signed forward error (at least a first-order
		estimate of it): The difference between the exact result of sampling
		and the actual result in radians. Note that all of these are subject
		to rounding error themselves. In the central case, zero is returned.*/
vec3 compute_projected_solid_angle_polygon_sampling_error(projected_solid_angle_polygon_t polygon, vec2 random_numbers, vec3 sampled_dir) {
	float target_projected_solid_angle = random_numbers[0] * polygon.projected_solid_angle;
	// In the central case, the sampling procedure is exact except for rounding
	// error
	if (is_central_case(polygon)) {
		return vec3(0.0f);
	}
	// In the other case, we repeat some computations to find the error
	else {
		// Select a sector and copy the relevant attributes
		float sector_projected_solid_angle;
		vec2 outer_ellipse;
		vec2 inner_ellipse = polygon.inner_ellipse_0;
		vec2 dir_0;
		[[unroll]]
		for (uint i = 0; i != MAX_POLYGON_VERTEX_COUNT - 1; ++i) {
			if ((i > 1 && i + 1 == polygon.vertex_count) || (i > 0 && target_projected_solid_angle < 0.0f))
				break;
			sector_projected_solid_angle = polygon.sector_projected_solid_angles[i];
			target_projected_solid_angle -= sector_projected_solid_angle;
			vec2 vertex_ellipse = polygon.ellipses[i];
			bool vertex_inner = is_inner_ellipse(vertex_ellipse);
			if (i == 0) {
				outer_ellipse = vertex_ellipse;
			}
			else {
				inner_ellipse = vertex_inner ? vertex_ellipse : inner_ellipse;
				outer_ellipse = vertex_inner ? outer_ellipse : vertex_ellipse;
			}
			dir_0 = polygon.vertices[i];
		}
		target_projected_solid_angle += sector_projected_solid_angle;
		// Compute the area in the sector from sampled_dir and dir_0 between
		// the two ellipses
		float sampled_projected_solid_angle = get_area_between_ellipses_in_sector(
			inner_ellipse, get_ellipse_rsqrt_det(inner_ellipse),
			outer_ellipse, get_ellipse_rsqrt_det(outer_ellipse),
			dir_0, sampled_dir.xy);
		// Compute error in the projected solid angle
		float scaled_backward_error = target_projected_solid_angle - sampled_projected_solid_angle;
		float backward_error = scaled_backward_error / polygon.projected_solid_angle;
		// Evaluate the derivative of the sampled direction with respect to the
		// projected solid angle
		mat2 constraint_matrix;
		vec2 inner_dir = ellipse_transform(inner_ellipse, sampled_dir.xy);
		vec2 outer_dir = ellipse_transform(outer_ellipse, sampled_dir.xy);
		float inner_factor = 1.0f / dot(sampled_dir.xy, inner_dir);
		float outer_factor = 1.0f / dot(sampled_dir.xy, outer_dir);
		constraint_matrix[0] = 0.5f * (inner_factor - outer_factor) * rotate_90(sampled_dir.xy);
		constraint_matrix[1] = ((1.0f - random_numbers[1]) / (inner_factor * inner_factor)) * inner_dir;
		constraint_matrix[1] += (random_numbers[1] / (outer_factor * outer_factor)) * outer_dir;
		constraint_matrix = transpose(constraint_matrix);
		vec3 sample_derivative;
		sample_derivative.xy = (1.0f / determinant(constraint_matrix)) * vec2(constraint_matrix[1][1], -constraint_matrix[0][1]);
		sample_derivative.z = -dot(sampled_dir.xy, sample_derivative.xy) / sampled_dir.z;
		// Evaluate the forward error in radians
		float forward_error = length(sample_derivative) * scaled_backward_error;
		// Return both errors
		return vec3(backward_error, scaled_backward_error, forward_error);
	}
}
