/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
#pragma once
#ifndef STUDIO_H
#define STUDIO_H

/*
==============================================================================

STUDIO MODELS

Studio models are position independent, so the cache manager can move them.
==============================================================================
*/

// header
#define STUDIO_VERSION	10
#define IDSTUDIOHEADER	(('T'<<24)+('S'<<16)+('D'<<8)+'I') // little-endian "IDST"
#define IDSEQGRPHEADER	(('Q'<<24)+('S'<<16)+('D'<<8)+'I') // little-endian "IDSQ"

// studio limits
#define MAXSTUDIOVERTS		16384	// max vertices per submodel
#define MAXSTUDIOSEQUENCES		256	// total animation sequences
#define MAXSTUDIOSKINS		256	// total textures
#define MAXSTUDIOSRCBONES		512	// bones allowed at source movement
#define MAXSTUDIOBONES		128	// total bones actually used
#define MAXSTUDIOMODELS		32	// sub-models per model
#define MAXSTUDIOBODYPARTS		32	// body parts per submodel
#define MAXSTUDIOGROUPS		16	// sequence groups (e.g. barney01.mdl, barney02.mdl, e.t.c)
#define MAXSTUDIOMESHES		256	// max textures per model
#define MAXSTUDIOCONTROLLERS		32	// max controllers per model
#define MAXSTUDIOATTACHMENTS		64	// max attachments per model
#define MAXSTUDIOBONEWEIGHTS		4	// absolute hardware limit!
#define MAXSTUDIONAME		32	// a part of specs
#define MAXSTUDIOPOSEPARAM		24
#define MAX_STUDIO_LIGHTMAP_SIZE	256	// must match with engine const!!!

// client-side model flags
#define STUDIO_ROCKET		(1U<<0)	// leave a trail
#define STUDIO_GRENADE		(1U<<1)	// leave a trail
#define STUDIO_GIB		(1U<<2)	// leave a trail
#define STUDIO_ROTATE		(1U<<3)	// rotate (bonus items)
#define STUDIO_TRACER		(1U<<4)	// green split trail
#define STUDIO_ZOMGIB		(1U<<5)	// small blood trail
#define STUDIO_TRACER2		(1U<<6)	// orange split trail + rotate
#define STUDIO_TRACER3		(1U<<7)	// purple trail
#define STUDIO_AMBIENT_LIGHT		(1U<<8)	// force to use ambient shading
#define STUDIO_TRACE_HITBOX		(1U<<9)	// always use hitbox trace instead of bbox
#define STUDIO_FORCE_SKYLIGHT		(1U<<10)	// always grab lightvalues from the sky settings (even if sky is invisible)

#define STUDIO_HAS_BUMP			(1U<<16)	// loadtime set
#define STUDIO_STATIC_PROP		(1U<<29)	// hint for engine
#define STUDIO_HAS_BONEINFO		(1U<<30)	// extra info about bones (pose matrix, procedural index etc)
#define STUDIO_HAS_BONEWEIGHTS	(1U<<31)	// yes we got support of bone weighting

// lighting & rendermode options
#define STUDIO_NF_FLATSHADE		0x0001
#define STUDIO_NF_CHROME		0x0002
#define STUDIO_NF_FULLBRIGHT		0x0004
#define STUDIO_NF_NOMIPS		0x0008	// ignore mip-maps
#define STUDIO_NF_SMOOTH		0x0010	// smooth tangent space
#define STUDIO_NF_ADDITIVE		0x0020	// rendering with additive mode
#define STUDIO_NF_MASKED		0x0040	// use texture with alpha channel
#define STUDIO_NF_NORMALMAP		0x0080	// indexed normalmap

#define STUDIO_NF_GLOSSMAP		0x0100	// glossmap
#define STUDIO_NF_GLOSSPOWER		0x0200
#define STUDIO_NF_LUMA			0x0400	// self-illuminate parts
#define STUDIO_NF_ALPHASOLID		0x0800	// use with STUDIO_NF_MASKED to have solid alphatest surfaces for env_static
#define STUDIO_NF_TWOSIDE		0x1000	// render mesh as twosided
#define STUDIO_NF_HEIGHTMAP		0x2000

#define STUDIO_NF_NODRAW		(1U<<16)	// failed to create shader for this mesh
#define STUDIO_NF_NODLIGHT		(1U<<17)	// failed to create dlight shader for this mesh
#define STUDIO_NF_NOSUNLIGHT		(1U<<18)	// failed to create sun light shader for this mesh

#define STUDIO_NF_HAS_ALPHA		(1U<<20)	// external texture has alpha-channel
#define STUDIO_NF_HAS_DETAIL		(1U<<21)	// studiomodels has detail textures
#define STUDIO_NF_COLORMAP		(1U<<30)	// internal system flag
#define STUDIO_NF_UV_COORDS		(1U<<31)	// using half-float coords instead of ST

// motion flags
#define STUDIO_X			0x0001
#define STUDIO_Y			0x0002
#define STUDIO_Z			0x0004
#define STUDIO_XR			0x0008
#define STUDIO_YR			0x0010
#define STUDIO_ZR			0x0020
#define STUDIO_LX			0x0040
#define STUDIO_LY			0x0080
#define STUDIO_LZ			0x0100
#define STUDIO_LXR			0x0200
#define STUDIO_LYR			0x0400
#define STUDIO_LZR			0x0800
#define STUDIO_LINEAR			0x1000
#define STUDIO_QUADRATIC_MOTION		0x2000
#define STUDIO_RESERVED			0x4000	// g-cont. reserved one bit for me
#define STUDIO_TYPES			0x7FFF
#define STUDIO_RLOOP			0x8000	// controller that wraps shortest distance

// bonecontroller types
#define STUDIO_MOUTH		4	// hardcoded

// sequence flags
#define STUDIO_LOOPING		0x0001	// ending frame should be the same as the starting frame
#define STUDIO_SNAP			0x0002	// do not interpolate between previous animation and this one
#define STUDIO_DELTA		0x0004	// this sequence "adds" to the base sequences, not slerp blends
#define STUDIO_AUTOPLAY		0x0008	// temporary flag that forces the sequence to always play
#define STUDIO_POST			0x0010	//
#define STUDIO_ALLZEROS		0x0020	// this animation/sequence has no real animation data
#define STUDIO_BLENDPOSE		0x0040   	// to differentiate GoldSrc style blending from Source style blending (with pose parameters)
#define STUDIO_CYCLEPOSE		0x0080	// cycle index is taken from a pose parameter index
#define STUDIO_REALTIME		0x0100	// cycle index is taken from a real-time clock, not the animations cycle index
#define STUDIO_LOCAL		0x0200	// sequence has a local context sequence
#define STUDIO_HIDDEN		0x0400	// don't show in default selection views
#define STUDIO_IKRULES		0x0800	// sequence has IK-rules
#define STUDIO_ACTIVITY		0x1000	// Has been updated at runtime to activity index
#define STUDIO_EVENT		0x2000	// Has been updated at runtime to event index
#define STUDIO_WORLD		0x4000	// sequence blends in worldspace
#define STUDIO_LIGHT_FROM_ROOT	0x8000	// get lighting point from root bonepos not from entity origin

// autolayer flags
#define STUDIO_AL_POST		0x0001	//
#define STUDIO_AL_SPLINE	0x0002	// convert layer ramp in/out curve is a spline instead of linear
#define STUDIO_AL_XFADE		0x0004	// pre-bias the ramp curve to compense for a non-1 weight,
					// assuming a second layer is also going to accumulate
#define STUDIO_AL_NOBLEND	0x0008	// animation always blends at 1.0 (ignores weight)
#define STUDIO_AL_LOCAL		0x0010	// layer is a local context sequence
#define STUDIO_AL_POSE		0x0020	// layer blends using a pose parameter instead of parent cycle

typedef struct studiohdr_s
{
	// the model signature
	int32_t		ident;

	// studio model format version
	int32_t		version;

	// the model name
	char		name[64];

	// the total file size in bytes
	int32_t		length;

	// ideal eye position
	vec3_t		eyeposition;

	// ideal movement hull size
	vec3_t		min;
	vec3_t		max;

	// clipping bounding box
	vec3_t		bbmin;
	vec3_t		bbmax;

	// undocumented quake features flags
	int32_t		flags;

	// the number of bones
	int32_t		numbones;

	// offset to the first bone chunk
	int32_t		boneindex;

	// the number of bone controllers
	int32_t		numbonecontrollers;

	// offset to the first bone controller chunk
	int32_t		bonecontrollerindex;

	// the number of hitboxes
	int32_t		numhitboxes;

	// offset to the first hitbox chunk
	int32_t		hitboxindex;

	// the number of sequences
	int32_t		numseq;

	// offset to the first sequence description chunk
	int32_t		seqindex;

	// the number of sequence groups
	int32_t		numseqgroups;

	// offset to the first sequence group chunk
	int32_t		seqgroupindex;

	// the number of textures
	int32_t		numtextures;

	// offset to the first texture chunk
	int32_t		textureindex;

	// offset to the first texture's image data
	int32_t		texturedataindex;

	// the number of replaceable textures
	int32_t		numskinref;

	// the number of skin families
	int32_t		numskinfamilies;

	// offset to the first replaceable texture
	int32_t		skinindex;

	// the number of bodyparts
	int32_t		numbodyparts;

	// offset to the first bodypart
	int32_t		bodypartindex;

	// the number of attachments
	int32_t		numattachments;

	// offset to the first attachment chunk
	int32_t		attachmentindex;

	// offset to the second studio model header
	int32_t		studiohdr2index;

	// was "soundindex"
	int32_t		unused;

	// was "soundgroups"
	int32_t		unused2;

	// was "soundgroupindex"
	int32_t		unused3;

	// the number of nodes in the sequence transition graph
	int32_t		numtransitions;

	// offset to the first sequence transition
	int32_t		transitionindex;
} studiohdr_t;

// extra header to hold more offsets
typedef struct
{
	// number of pose parameters
	int32_t		numposeparameters;

	// offset to the first pose parameter
	int32_t		poseparamindex;

	// number of IK-autoplaying locks
	int32_t		numikautoplaylocks;

	// offset to the first IK-autoplaying lock
	int32_t		ikautoplaylockindex;

	// number of IK-chains
	int32_t		numikchains;

	// offset to the first IK-chain
	int32_t		ikchainindex;

	// offset to the first key-value
	int32_t		keyvalueindex;

	// size of key-values
	int32_t		keyvaluesize;

	// number of hitbox sets
	int32_t		numhitboxsets;

	// offset to the first hitbox set
	int32_t		hitboxsetindex;

	// for future expansions
	int32_t		unused[6];
} studiohdr2_t;

// header for demand loaded sequence group data
typedef struct
{
	// the model signature
	int32_t		id;

	// studio model format version
	int32_t		version;

	// the sequence group file name
	char		name[64];

	// the total file size in bytes
	int32_t		length;
} studioseqhdr_t;

// bone flags
#define BONE_ALWAYS_PROCEDURAL		0x0001	// bone is always procedurally animated
#define BONE_SCREEN_ALIGN_SPHERE	0x0002	// bone aligns to the screen, not constrained in motion.
#define BONE_SCREEN_ALIGN_CYLINDER	0x0004	// bone aligns to the screen, constrained by it's own axis.
#define BONE_JIGGLE_PROCEDURAL		0x0008
#define BONE_FIXED_ALIGNMENT		0x0010	// bone can't spin 360 degrees, all interpolation is normalized around a fixed orientation

#define BONE_USED_MASK		(BONE_USED_BY_HITBOX|BONE_USED_BY_ATTACHMENT|BONE_USED_BY_VERTEX|BONE_USED_BY_BONE_MERGE)
#define BONE_USED_BY_ANYTHING		BONE_USED_MASK
#define BONE_USED_BY_HITBOX		0x00000100	// bone (or child) is used by a hit box
#define BONE_USED_BY_ATTACHMENT		0x00000200	// bone (or child) is used by an attachment point
#define BONE_USED_BY_VERTEX		0x00000400	// bone (or child) is used by the toplevel model via skinned vertex
#define BONE_USED_BY_BONE_MERGE		0x00000800

// bones
typedef struct mstudiobone_s
{
	// the bone name
	char		name[MAXSTUDIONAME];

	// the parent bone index. (-1) If it has no parent
	int32_t		parent;

	// was "flags"
	int32_t		unused;

	// 1vailable bone controller per motion type.
	// (-1) if no controller is available.
	int32_t		bonecontroller[6];

	/* default position and rotation values where
	 * scale[0] = position.X
	 * scale[1] = position.Y
	 * scale[2] = position.Z
	 * scale[3] = rotation.X
	 * scale[4] = rotation.Y
	 * scale[5] = rotation.Z
	 */
	vec_t		value[6];

	/* compressed scale values where
	 * scale[0] = position.X scale
	 * scale[1] = position.Y scale
	 * scale[2] = position.Z scale
	 * scale[3] = rotation.X scale
	 * scale[4] = rotation.Y scale
	 * scale[5] = rotation.Z scale
	 */
	vec_t		scale[6];
} mstudiobone_t;

#define STUDIO_PROC_AXISINTERP	1
#define STUDIO_PROC_QUATINTERP	2
#define STUDIO_PROC_AIMATBONE	3
#define STUDIO_PROC_AIMATATTACH	4
#define STUDIO_PROC_JIGGLE	5

typedef struct
{
	// local transformation of this bone used to calc 3 point blend
	int32_t		control;

	// axis to check
	int32_t		axis;

	// X+, X-, Y+, Y-, Z+, Z-
	vec3_t		pos[6];

	// X+, X-, Y+, Y-, Z+, Z-
	vec4_t		quat[6];
} mstudioaxisinterpbone_t;

typedef struct
{
	// 1.0f / radian angle of trigger influence
	vec_t		inv_tolerance;

	// angle to match
	vec4_t		trigger;

	// new position
	vec3_t		pos;

	// new angle
	vec4_t		quat;
} mstudioquatinterpinfo_t;

typedef struct
{
	// local transformation to check
	int32_t		control;
	int32_t		numtriggers;
	int32_t		triggerindex;
} mstudioquatinterpbone_t;

// extra info for bones
typedef struct
{
	// boneweighting reqiures
	vec_t		poseToBone[3][4];

	vec4_t		qAlignment;

	int32_t		proctype;

	// procedural rule
	int32_t		procindex;

	// aligned bone rotation
	vec4_t		quat;

	// for future expansions
	int32_t		reserved[10];
} mstudioboneinfo_t;

// JIGGLEBONES
#define JIGGLE_IS_FLEXIBLE		0x01
#define JIGGLE_IS_RIGID			0x02
#define JIGGLE_HAS_YAW_CONSTRAINT	0x04
#define JIGGLE_HAS_PITCH_CONSTRAINT	0x08
#define JIGGLE_HAS_ANGLE_CONSTRAINT	0x10
#define JIGGLE_HAS_LENGTH_CONSTRAINT	0x20
#define JIGGLE_HAS_BASE_SPRING		0x40
#define JIGGLE_IS_BOING			0x80	// simple squash and stretch sinusoid "boing"

typedef struct
{
	int32_t		flags;

	// general params
	vec_t		length;		// how from from bone base, along bone, is tip
	vec_t		tipMass;

	// flexible params
	vec_t		yawStiffness;
	vec_t		yawDamping;
	vec_t		pitchStiffness;
	vec_t		pitchDamping;
	vec_t		alongStiffness;
	vec_t		alongDamping;

	// angle constraint
	vec_t		angleLimit;	// maximum deflection of tip in radians

	// yaw constraint
	vec_t		minYaw;		// in radians
	vec_t		maxYaw;		// in radians
	vec_t		yawFriction;
	vec_t		yawBounce;

	// pitch constraint
	vec_t		minPitch;		// in radians
	vec_t		maxPitch;		// in radians
	vec_t		pitchFriction;
	vec_t		pitchBounce;

	// base spring
	vec_t		baseMass;
	vec_t		baseStiffness;
	vec_t		baseDamping;
	vec_t		baseMinLeft;
	vec_t		baseMaxLeft;
	vec_t		baseLeftFriction;
	vec_t		baseMinUp;
	vec_t		baseMaxUp;
	vec_t		baseUpFriction;
	vec_t		baseMinForward;
	vec_t		baseMaxForward;
	vec_t		baseForwardFriction;

	// boing
	vec_t		boingImpactSpeed;
	vec_t		boingImpactAngle;
	vec_t		boingDampingRate;
	vec_t		boingFrequency;
	vec_t		boingAmplitude;
} mstudiojigglebone_t;

typedef struct
{
	int32_t		parent;

	// might be bone or attach
	int32_t		aim;

	vec3_t		aimvector;
	vec3_t		upvector;
	vec3_t		basepos;
} mstudioaimatbone_t;

// bone controllers
typedef struct
{
	// bone affected by this controller
	int32_t		bone;

	// the motion type
	int32_t		type;

	// the minimum and maximum values
	vec_t		start;
	vec_t		end;

	// was "rest"
	int32_t		unused;

	// the bone controller channel
	int32_t		index;
} mstudiobonecontroller_t;

// intersection boxes
typedef struct
{
	// the bone this hitbox follows
	int32_t		bone;

	// the hit group
	int32_t		group;

	// the hitbox minimum and maximum extents
	vec3_t		bbmin;
	vec3_t		bbmax;
} mstudiobbox_t;

typedef struct
{
	char		name[MAXSTUDIONAME];
	int32_t		numhitboxes;
	int32_t		hitboxindex;
} mstudiohitboxset_t;

// demand loaded sequence groups
typedef struct
{
	// a textual name for this sequence group
	char		label[MAXSTUDIONAME];

	// the file name
	char		name[64];

	// was "cache"
	int32_t		unused;

	// was "data"
	int32_t		unused2;
} mstudioseqgroup_t;

// events
#include "studio_event.h"

#define STUDIO_ATTACHMENT_LOCAL	(1<<0)	// vectors are filled

// attachment
typedef struct
{
	// was "name"
	char		unused[MAXSTUDIONAME];

	int32_t		flags;

	// the bone this attachment follows
	int32_t		bone;

	// the attachment origin
	vec3_t		org;

	// the attachment vectors
	vec3_t		vectors[3];
} mstudioattachment_t;

#define IK_SELF		1
#define IK_WORLD		2
#define IK_GROUND		3
#define IK_RELEASE		4
#define IK_ATTACHMENT	5
#define IK_UNLATCH		6

typedef struct
{
	vec_t		scale[6];
	uint16_t	offset[6];
} mstudioikerror_t;

typedef struct
{
	int32_t		index;

	int32_t		type;
	int32_t		chain;

	int32_t		bone;

	// offset to the attachment
	int32_t		attachment;

	// iktarget slot
	// usually same as chain
	int32_t		slot;

	vec_t		height;
	vec_t		radius;
	vec_t		floor;
	vec3_t		pos;
	vec4_t		quat;

	// offset to the compressed IK error
	int32_t		ikerrorindex;

	int32_t		iStart;

	// beginning of influence
	vec_t		start;

	// start of full influence
	vec_t		peak;

	// end of full influence
	vec_t		tail;

	// end of all influence
	vec_t		end;

	// frame footstep makes ground concact
	vec_t		contact;

	// how far down the foot should drop when reaching for IK
	vec_t		drop;

	// top of the foot box
	vec_t		top;

	// for future expansions
	int32_t		unused[4];
} mstudioikrule_t;

typedef struct
{
	int32_t		chain;
	vec_t		flPosWeight;
	vec_t		flLocalQWeight;
	int32_t		flags;

	// for future expansions
	int32_t		unused[4];
} mstudioiklock_t;

typedef struct
{
	int32_t		endframe;

	int32_t		motionflags;

	// velocity at start of block
	vec_t		v0;

	// velocity at end of block
	vec_t		v1;

	// YAW rotation at end of this blocks movement
	vec_t		angle;

	// movement vector relative to this blocks initial angle
	vec3_t		vector;

	// relative to start of animation???
	vec3_t		position;
} mstudiomovement_t;

// additional info for each animation in sequence blend group or single sequence
typedef struct
{
	// animation label (may be matched with sequence label)
	char		label[MAXSTUDIONAME];

	// frames per second (match with sequence fps or be different)
	vec_t		fps;

	// looping/non-looping flags
	int32_t		flags;

	// number of frames per animation
	int32_t		numframes;

	// number of piecewise movements
	int32_t		nummovements;

	// offset to the first piecewise movement
	int32_t		movementindex;

	// number of IK-rules
	int32_t		numikrules;

	// offset to the first IK-rule
	int32_t		ikruleindex;

	// for future expansions
	int32_t		unused[8];
} mstudioanimdesc_t;

// autoplaying sequences
typedef struct
{
	int16_t		iSequence;
	int16_t		iPose;
	int32_t		flags;

	// beginning of influence
	vec_t		start;

	// start of full influence
	vec_t		peak;

	// end of full influence
	vec_t		tail;

	// end of all influence
	vec_t		end;
} mstudioautolayer_t;

// sequence descriptions
typedef struct mstudioseqdesc_s
{
	// the sequence name
	char		label[MAXSTUDIONAME];

	// frames per second
	vec_t		fps;

	// looping/non-looping flags
	int32_t		flags;

	// the sequence activity
	int32_t		activity;

	// the sequence activity weight
	int32_t		actweight;

	// the number of animation events
	int32_t		numevents;

	// offset to the first animation event chunk
	int32_t		eventindex;

	// the number of frames in the sequence
	int32_t		numframes;

	// offset to the first weight list
	int32_t		weightlistindex;

	// offset to the first IK lock
	int32_t		iklockindex;

	// linear motion type
	int32_t		motiontype;

	// offset to the first pose parameter
	int32_t		motionbone;

	// linear motion
	vec3_t		linearmovement;

	// offset to the first autolayer description
	int32_t		autolayerindex;

	// offset to the first local key-value
	int32_t		keyvalueindex;

	// the sequence minimum and maximum extents
	vec3_t		bbmin;
	vec3_t		bbmax;

	// The number of blend animations
	int32_t		numblends;

	// offset to thefirst mstudioanim_t chunk.
	// this offset is relative to the studioseqhdr_t of the file
	// that contains the animation data.
	int32_t		animindex;

	// the motion type of each blend controller
	int32_t		blendtype[2];

	// the starting value of each blend controller
	vec_t		blendstart[2];

	// the ending value of each blend controller
	vec_t		blendend[2];

	// 255 x 255 blends should be enough
	uint8_t		groupsize[2];

	// number of autoplaying layers
	uint8_t		numautolayers;

	// number of IK-locks per sequence
	uint8_t		numiklocks;

	// the sequence group
	int32_t		seqgroup;

	// the node at entry in the sequence transition graph
	int32_t		entrynode;

	// the node at exit in the sequence transition graph
	int32_t		exitnode;

	// transition rules
	uint8_t		nodeflags;

	// index of pose parameter to use as cycle index
	uint8_t		cycleposeindex;

	// ideal cross fade in time (0.2 secs default) time = (fadeintime / 100)
	uint8_t		fadeintime;

	// ideal cross fade out time (0.2 msecs default)  time = (fadeouttime / 100)
	uint8_t		fadeouttime;

	// mstudioanimdesc_t [blend]
	int32_t		animdescindex;
} mstudioseqdesc_t;

typedef struct
{
	// pose parameter name
	char		name[MAXSTUDIONAME];

	// ????
	int32_t		flags;

	// starting value
	vec_t		start;

	// ending value
	vec_t		end;

	// looping range, 0 for no looping, 360 for rotations, etc
	vec_t		loop;
} mstudioposeparamdesc_t;

// offsets to the animation frames
typedef struct mstudioanim_s
{
	uint16_t	offset[6];
} mstudioanim_t;

// animation frames
typedef union
{
	struct
	{
		uint8_t	valid;
		uint8_t	total;
	} num;
	int16_t		value;
} mstudioanimvalue_t;

// body part index
typedef struct
{
	// the bodypart name
	char		name[64];

	// the number of available models for this bodypart
	int32_t		nummodels;

	// used to convert from a global model index
	// to a local bodypart model index
	int32_t		base;

	// the offset to the first model chunk
	int32_t		modelindex;	// index into models array
} mstudiobodyparts_t;

// skin info
typedef struct mstudiotex_s
{
	// texture file name
	char		name[64];

	// texture flags
	uint32_t	flags;

	// texture width in pixels
	int32_t		width;

	// texture height in pixels
	int32_t		height;

	// offset to the image data
	// this offset is relative to the texture file header
	int32_t		index;
} mstudiotexture_t;

// ikinfo
typedef struct
{
	int32_t		bone;

	// ideal bending direction (per link, if applicable)
	vec3_t		kneeDir;

	// unused
	vec3_t		unused0;
} mstudioiklink_t;

typedef struct
{
	char		name[MAXSTUDIONAME];
	int32_t		linktype;
	int32_t		numlinks;
	int32_t		linkindex;
} mstudioikchain_t;

typedef struct
{
	uint8_t		weight[4];
	int8_t		bone[4];
} mstudioboneweight_t;

// skin families
// short	index[skinfamilies][skinref]

// studio models
typedef struct
{
	// model name
	char		name[64];

	// was "type"
	int32_t		unused;

	// was "boundingradius"
	vec_t		unused2;

	// the number of meshes in the model
	int32_t		nummesh;

	// offset to the first mesh chunk
	int32_t		meshindex;

	// the number of unique vertices
	int32_t		numverts;

	// offset to the vertex bone array
	int32_t		vertinfoindex;

	// offset to the vertex array
	int32_t		vertindex;

	// the number of unique normals
	int32_t		numnorms;

	// offset to the normal bone array
	int32_t		norminfoindex;

	// offset to the normal array
	int32_t		normindex;

	// offset to the boneweighted vertex info
	int32_t		blendvertinfoindex;

	// offset to the boneweighted normal info
	int32_t		blendnorminfoindex;
} mstudiomodel_t;

// vec3_t	boundingbox[model][bone][2];		// complex intersection info

// meshes
typedef struct
{
	// can be interpreted as the number of triangles in the mesh
	int32_t		numtris;

	// offset to the start of the tris sequence
	int32_t		triindex;

	// the skin index
	int32_t		skinref;

	// the number of normals in the mesh
	int32_t		numnorms;

	// was "normindex"
	int32_t		unused;
} mstudiomesh_t;

// triangles
typedef struct
{
	// index into vertex array
	int16_t		vertindex;

	// index into normal array
	int16_t		normindex;

	// texture coordinates in absolute space (unnormalized)
	int16_t		s,t;
} mstudiotrivert_t;

#endif//STUDIO_H
