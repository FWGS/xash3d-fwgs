#pragma once

#include <skygfx/utils.h>
#include <any>

using GLubyte = uint8_t;
using GLenum = uint32_t;
using GLint = int32_t;
using GLuint = uint32_t;
using GLsizei = int32_t;
using GLfloat = float;
using GLdouble = double;
using GLboolean = uint8_t;
using GLvoid = void;
using GLsizeiptrARB = int32_t;
using GLintptrARB = int32_t;
using GLcharARB = char;

#define GL_DONT_CARE			0x1100
#define GL_FASTEST				0x1101
#define GL_NICEST				0x1102

#define GL_TEXTURE_1D_ARRAY_EXT		0x8C18
#define GL_PROXY_TEXTURE_1D_ARRAY_EXT		0x8C19
#define GL_TEXTURE_2D_ARRAY_EXT		0x8C1A
#define GL_PROXY_TEXTURE_2D_ARRAY_EXT		0x8C1B
#define GL_TEXTURE_BINDING_1D_ARRAY_EXT		0x8C1C
#define GL_TEXTURE_BINDING_2D_ARRAY_EXT		0x8C1D
#define GL_MAX_ARRAY_TEXTURE_LAYERS_EXT		0x88FF
#define GL_COMPARE_REF_DEPTH_TO_TEXTURE_EXT	0x884E

#define GL_PACK_SKIP_IMAGES			0x806B
#define GL_PACK_IMAGE_HEIGHT			0x806C
#define GL_UNPACK_SKIP_IMAGES			0x806D
#define GL_UNPACK_IMAGE_HEIGHT		0x806E
#define GL_TEXTURE_3D			0x806F
#define GL_PROXY_TEXTURE_3D			0x8070
#define GL_TEXTURE_DEPTH			0x8071
#define GL_TEXTURE_WRAP_R			0x8072
#define GL_MAX_3D_TEXTURE_SIZE		0x8073
#define GL_TEXTURE_BINDING_3D			0x806A
#define GL_TEXTURE_CUBE_MAP_SEAMLESS		0x884F
#define GL_STENCIL_TEST_TWO_SIDE_EXT		0x8910
#define GL_ACTIVE_STENCIL_FACE_EXT		0x8911
#define GL_STENCIL_BACK_FUNC              	0x8800
#define GL_STENCIL_BACK_FAIL              	0x8801
#define GL_STENCIL_BACK_PASS_DEPTH_FAIL   	0x8802
#define GL_STENCIL_BACK_PASS_DEPTH_PASS   	0x8803

#define GL_POINT_SMOOTH			0x0B10
#define GL_LINE_SMOOTH			0x0B20
#define GL_POLYGON_SMOOTH			0x0B41
#define GL_POLYGON_STIPPLE			0x0B42
#define GL_CLIP_PLANE0			0x3000
#define GL_CLIP_PLANE1			0x3001
#define GL_CLIP_PLANE2			0x3002
#define GL_CLIP_PLANE3			0x3003
#define GL_CLIP_PLANE4			0x3004
#define GL_CLIP_PLANE5			0x3005
#define GL_POINT_SIZE_MIN_EXT			0x8126
#define GL_POINT_SIZE_MAX_EXT			0x8127
#define GL_POINT_FADE_THRESHOLD_SIZE_EXT	0x8128
#define GL_DISTANCE_ATTENUATION_EXT		0x8129
#define GL_ACTIVE_TEXTURE_ARB			0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB		0x84E1
#define GL_MAX_TEXTURE_UNITS_ARB		0x84E2
#define GL_TEXTURE0_ARB			0x84C0
#define GL_TEXTURE1_ARB			0x84C1
#define GL_TEXTURE2_ARB                   	0x84C2
#define GL_TEXTURE0_SGIS			0x835E
#define GL_TEXTURE1_SGIS			0x835F
#define GL_GENERATE_MIPMAP_SGIS           	0x8191
#define GL_GENERATE_MIPMAP_HINT_SGIS      	0x8192
#define GL_TEXTURE_RECTANGLE_NV		0x84F5
#define GL_TEXTURE_BINDING_RECTANGLE_NV		0x84F6
#define GL_PROXY_TEXTURE_RECTANGLE_NV		0x84F7
#define GL_MAX_RECTANGLE_TEXTURE_SIZE_NV	0x84F8
#define GL_TEXTURE_RECTANGLE_EXT		0x84F5
#define GL_TEXTURE_BINDING_RECTANGLE_EXT	0x84F6
#define GL_PROXY_TEXTURE_RECTANGLE_EXT		0x84F7
#define GL_MAX_RECTANGLE_TEXTURE_SIZE_EXT	0x84F8
#define GL_MAX_TEXTURE_UNITS			0x84E2
#define GL_MAX_TEXTURE_UNITS_ARB		0x84E2

#define GL_VERTEX_SHADER_ARB			0x8B31
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB	0x8B4A
#define GL_MAX_VARYING_FLOATS_ARB		0x8B4B
#define GL_MAX_VERTEX_ATTRIBS_ARB		0x8869
#define GL_MAX_TEXTURE_IMAGE_UNITS_ARB		0x8872
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS_ARB	0x8B4C
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS_ARB	0x8B4D
#define GL_MAX_TEXTURE_COORDS_ARB		0x8871
#define GL_VERTEX_PROGRAM_POINT_SIZE_ARB	0x8642
#define GL_VERTEX_PROGRAM_TWO_SIDE_ARB		0x8643
#define GL_OBJECT_ACTIVE_ATTRIBUTES_ARB		0x8B89
#define GL_OBJECT_ACTIVE_ATTRIBUTE_MAX_LENGTH_ARB	0x8B8A
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED_ARB	0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE_ARB		0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE_ARB	0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE_ARB		0x8625
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED_ARB	0x886A
#define GL_CURRENT_VERTEX_ATTRIB_ARB		0x8626
#define GL_VERTEX_ATTRIB_ARRAY_POINTER_ARB	0x8645
#define GL_FLOAT_VEC2_ARB			0x8B50
#define GL_FLOAT_VEC3_ARB			0x8B51
#define GL_FLOAT_VEC4_ARB			0x8B52
#define GL_FLOAT_MAT2_ARB			0x8B5A
#define GL_FLOAT_MAT3_ARB			0x8B5B
#define GL_FLOAT_MAT4_ARB			0x8B5C

#define GL_MULTISAMPLE_ARB			0x809D
#define GL_SAMPLE_ALPHA_TO_COVERAGE_ARB		0x809E
#define GL_SAMPLE_ALPHA_TO_ONE_ARB		0x809F
#define GL_SAMPLE_COVERAGE_ARB		0x80A0
#define GL_SAMPLE_BUFFERS_ARB			0x80A8
#define GL_SAMPLES_ARB			0x80A9
#define GL_SAMPLE_COVERAGE_VALUE_ARB		0x80AA
#define GL_SAMPLE_COVERAGE_INVERT_ARB		0x80AB
#define GL_MULTISAMPLE_BIT_ARB		0x20000000
#define GL_TEXTURE_2D_MULTISAMPLE			0x9100

#define GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB		0x8242
#define GL_MAX_DEBUG_MESSAGE_LENGTH_ARB		0x9143
#define GL_MAX_DEBUG_LOGGED_MESSAGES_ARB	0x9144
#define GL_DEBUG_LOGGED_MESSAGES_ARB		0x9145
#define GL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_ARB	0x8243
#define GL_DEBUG_CALLBACK_FUNCTION_ARB		0x8244
#define GL_DEBUG_CALLBACK_USER_PARAM_ARB	0x8245
#define GL_DEBUG_SOURCE_API_ARB		0x8246
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB	0x8247
#define GL_DEBUG_SOURCE_SHADER_COMPILER_ARB	0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY_ARB		0x8249
#define GL_DEBUG_SOURCE_APPLICATION_ARB		0x824A
#define GL_DEBUG_SOURCE_OTHER_ARB		0x824B
#define GL_DEBUG_TYPE_ERROR_ARB		0x824C
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB	0x824D
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB	0x824E
#define GL_DEBUG_TYPE_PORTABILITY_ARB		0x824F
#define GL_DEBUG_TYPE_PERFORMANCE_ARB		0x8250
#define GL_DEBUG_TYPE_OTHER_ARB		0x8251
#define GL_DEBUG_SEVERITY_HIGH_ARB		0x9146
#define GL_DEBUG_SEVERITY_MEDIUM_ARB		0x9147
#define GL_DEBUG_SEVERITY_LOW_ARB		0x9148

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT		0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT	0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT	0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT	0x83F3
#define GL_COMPRESSED_RGBA_BPTC_UNORM_ARB           0x8E8C
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB     0x8E8D
#define GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB     0x8E8E
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB   0x8E8F
#define GL_COMPRESSED_RED_GREEN_RGTC2_EXT	0x8DBD
#define GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI	0x8837
#define GL_COMPRESSED_ALPHA_ARB		0x84E9
#define GL_COMPRESSED_LUMINANCE_ARB		0x84EA
#define GL_COMPRESSED_LUMINANCE_ALPHA_ARB	0x84EB
#define GL_COMPRESSED_INTENSITY_ARB		0x84EC
#define GL_COMPRESSED_RGB_ARB			0x84ED
#define GL_COMPRESSED_RGBA_ARB		0x84EE
#define GL_TEXTURE_COMPRESSION_HINT_ARB		0x84EF
#define GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB	0x86A0
#define GL_TEXTURE_COMPRESSED_ARB		0x86A1
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS_ARB	0x86A2
#define GL_COMPRESSED_TEXTURE_FORMATS_ARB	0x86A3
#define GL_UNSIGNED_BYTE_2_3_3_REV		0x8362
#define GL_UNSIGNED_SHORT_5_6_5		0x8363
#define GL_UNSIGNED_SHORT_5_6_5_REV		0x8364
#define GL_UNSIGNED_SHORT_4_4_4_4_REV		0x8365
#define GL_UNSIGNED_SHORT_1_5_5_5_REV		0x8366
#define GL_UNSIGNED_INT_8_8_8_8_REV		0x8367
#define GL_UNSIGNED_INT_2_10_10_10_REV		0x8368
#define GL_TEXTURE_MAX_LEVEL			0x813D
#define GL_GENERATE_MIPMAP			0x8191
#define GL_ADD_SIGNED			0x8574

#define GL_MODELVIEW			0x1700
#define GL_PROJECTION			0x1701
#define GL_TEXTURE				0x1702
#define GL_MATRIX_MODE			0x0BA0
#define GL_MODELVIEW_MATRIX			0x0BA6
#define GL_PROJECTION_MATRIX			0x0BA7
#define GL_TEXTURE_MATRIX			0x0BA8

#define GL_COMBINE_ARB			0x8570
#define GL_COMBINE_RGB_ARB			0x8571
#define GL_COMBINE_ALPHA_ARB			0x8572
#define GL_SOURCE0_RGB_ARB			0x8580
#define GL_SOURCE1_RGB_ARB			0x8581
#define GL_SOURCE2_RGB_ARB			0x8582
#define GL_SOURCE0_ALPHA_ARB			0x8588
#define GL_SOURCE1_ALPHA_ARB			0x8589
#define GL_SOURCE2_ALPHA_ARB			0x858A
#define GL_OPERAND0_RGB_ARB			0x8590
#define GL_OPERAND1_RGB_ARB			0x8591
#define GL_OPERAND2_RGB_ARB			0x8592
#define GL_OPERAND0_ALPHA_ARB			0x8598
#define GL_OPERAND1_ALPHA_ARB			0x8599
#define GL_OPERAND2_ALPHA_ARB			0x859A
#define GL_RGB_SCALE_ARB			0x8573
#define GL_ADD_SIGNED_ARB			0x8574
#define GL_INTERPOLATE_ARB			0x8575
#define GL_SUBTRACT_ARB			0x84E7
#define GL_CONSTANT_ARB			0x8576
#define GL_PRIMARY_COLOR_ARB			0x8577
#define GL_PREVIOUS_ARB			0x8578

//GL_ARB_vertex_buffer_object
#define GL_ARRAY_BUFFER_ARB			0x8892
#define GL_ELEMENT_ARRAY_BUFFER_ARB		0x8893
#define GL_ARRAY_BUFFER_BINDING_ARB		0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB	0x8895
#define GL_VERTEX_ARRAY_BUFFER_BINDING_ARB	0x8896
#define GL_NORMAL_ARRAY_BUFFER_BINDING_ARB	0x8897
#define GL_COLOR_ARRAY_BUFFER_BINDING_ARB	0x8898
#define GL_INDEX_ARRAY_BUFFER_BINDING_ARB	0x8899
#define GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING_ARB	0x889A
#define GL_EDGE_FLAG_ARRAY_BUFFER_BINDING_ARB	0x889B
#define GL_WEIGHT_ARRAY_BUFFER_BINDING_ARB	0x889E
#define GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING_ARB	0x889F
#define GL_STREAM_DRAW_ARB			0x88E0
#define GL_STREAM_READ_ARB			0x88E1
#define GL_STREAM_COPY_ARB			0x88E2
#define GL_STATIC_DRAW_ARB			0x88E4
#define GL_STATIC_READ_ARB			0x88E5
#define GL_STATIC_COPY_ARB			0x88E6
#define GL_DYNAMIC_DRAW_ARB			0x88E8
#define GL_DYNAMIC_READ_ARB			0x88E9
#define GL_DYNAMIC_COPY_ARB			0x88EA
#define GL_READ_ONLY_ARB			0x88B8
#define GL_WRITE_ONLY_ARB			0x88B9
#define GL_READ_WRITE_ARB			0x88BA
#define GL_BUFFER_SIZE_ARB			0x8764
#define GL_BUFFER_USAGE_ARB			0x8765
#define GL_BUFFER_ACCESS_ARB			0x88BB
#define GL_BUFFER_MAPPED_ARB			0x88BC
#define GL_BUFFER_MAP_POINTER_ARB		0x88BD
#define GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING_ARB	0x889C
#define GL_FOG_COORDINATE_ARRAY_BUFFER_BINDING_ARB	0x889D

#define GL_NORMAL_MAP_ARB			0x8511
#define GL_REFLECTION_MAP_ARB			0x8512
#define GL_TEXTURE_CUBE_MAP_ARB		0x8513
#define GL_TEXTURE_BINDING_CUBE_MAP_ARB		0x8514
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB	0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X_ARB	0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y_ARB	0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_ARB	0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z_ARB	0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z_ARB	0x851A
#define GL_PROXY_TEXTURE_CUBE_MAP_ARB		0x851B
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB	0x851C

#define GL_BYTE				0x1400
#define GL_UNSIGNED_BYTE			0x1401
#define GL_SHORT				0x1402
#define GL_UNSIGNED_SHORT			0x1403
#define GL_INT				0x1404
#define GL_UNSIGNED_INT			0x1405
#define GL_FLOAT				0x1406
#define GL_DOUBLE				0x140A
#define GL_2_BYTES				0x1407
#define GL_3_BYTES				0x1408
#define GL_4_BYTES				0x1409
#define GL_HALF_FLOAT_ARB			0x140B

#define GL_TEXTURE_ENV			0x2300
#define GL_TEXTURE_ENV_MODE			0x2200
#define GL_TEXTURE_ENV_COLOR			0x2201
#define GL_TEXTURE_1D			0x0DE0
#define GL_TEXTURE_2D			0x0DE1
#define GL_TEXTURE_WRAP_S			0x2802
#define GL_TEXTURE_WRAP_T			0x2803
#define GL_TEXTURE_WRAP_R			0x8072
#define GL_TEXTURE_BORDER_COLOR		0x1004
#define GL_TEXTURE_MAG_FILTER			0x2800
#define GL_TEXTURE_MIN_FILTER			0x2801
#define GL_PACK_ALIGNMENT			0x0D05
#define GL_UNPACK_ALIGNMENT			0x0CF5
#define GL_TEXTURE_BINDING_1D			0x8068
#define GL_TEXTURE_BINDING_2D			0x8069
#define GL_CLAMP_TO_EDGE                  	0x812F
#define GL_CLAMP_TO_BORDER                	0x812D
#define GL_NEAREST				0x2600
#define GL_LINEAR				0x2601
#define GL_NEAREST_MIPMAP_NEAREST		0x2700
#define GL_NEAREST_MIPMAP_LINEAR		0x2702
#define GL_LINEAR_MIPMAP_NEAREST		0x2701
#define GL_LINEAR_MIPMAP_LINEAR		0x2703

#define GL_TEXTURE_MAX_ANISOTROPY_EXT		0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT	0x84FF

#define GL_MAX_TEXTURE_LOD_BIAS_EXT		0x84FD
#define GL_TEXTURE_FILTER_CONTROL_EXT		0x8500
#define GL_TEXTURE_LOD_BIAS_EXT		0x8501

#define GL_CLAMP_TO_BORDER_ARB		0x812D

#define GL_TEXTURE_RED_TYPE_ARB                   0x8C10
#define GL_TEXTURE_GREEN_TYPE_ARB                 0x8C11
#define GL_TEXTURE_BLUE_TYPE_ARB                  0x8C12
#define GL_TEXTURE_ALPHA_TYPE_ARB                 0x8C13
#define GL_TEXTURE_LUMINANCE_TYPE_ARB             0x8C14
#define GL_TEXTURE_INTENSITY_TYPE_ARB             0x8C15
#define GL_TEXTURE_DEPTH_TYPE_ARB                 0x8C16
#define GL_UNSIGNED_NORMALIZED_ARB                0x8C17
#define GL_RGBA32F_ARB                            0x8814
#define GL_RGB32F_ARB                             0x8815
#define GL_ALPHA32F_ARB                           0x8816
#define GL_INTENSITY32F_ARB                       0x8817
#define GL_LUMINANCE32F_ARB                       0x8818
#define GL_LUMINANCE_ALPHA32F_ARB                 0x8819
#define GL_RGBA16F_ARB                            0x881A
#define GL_RGB16F_ARB                             0x881B
#define GL_ALPHA16F_ARB                           0x881C
#define GL_INTENSITY16F_ARB                       0x881D
#define GL_LUMINANCE16F_ARB                       0x881E
#define GL_LUMINANCE_ALPHA16F_ARB                 0x881F

#define GL_ADD				0x0104
#define GL_DECAL				0x2101
#define GL_MODULATE				0x2100

#define GL_BLEND				0x0BE2
#define GL_BLEND_SRC				0x0BE1
#define GL_BLEND_DST				0x0BE0
#define GL_ZERO					0x0
#define GL_ONE					0x1
#define GL_SRC_COLOR				0x0300
#define GL_ONE_MINUS_SRC_COLOR			0x0301
#define GL_SRC_ALPHA				0x0302
#define GL_ONE_MINUS_SRC_ALPHA			0x0303
#define GL_DST_ALPHA				0x0304
#define GL_ONE_MINUS_DST_ALPHA			0x0305
#define GL_DST_COLOR				0x0306
#define GL_ONE_MINUS_DST_COLOR			0x0307
#define GL_SRC_ALPHA_SATURATE			0x0308

#define GL_NEVER				0x0200
#define GL_LESS				0x0201
#define GL_EQUAL				0x0202
#define GL_LEQUAL				0x0203
#define GL_GREATER				0x0204
#define GL_NOTEQUAL				0x0205
#define GL_GEQUAL				0x0206
#define GL_ALWAYS				0x0207
#define GL_DEPTH_TEST			0x0B71

#define GL_ALPHA_TEST			0x0BC0

#define GL_POINTS				0x0000
#define GL_LINES				0x0001
#define GL_LINE_LOOP			0x0002
#define GL_LINE_STRIP			0x0003
#define GL_TRIANGLES			0x0004
#define GL_TRIANGLE_STRIP			0x0005
#define GL_TRIANGLE_FAN			0x0006
#define GL_QUADS				0x0007
#define GL_QUAD_STRIP			0x0008
#define GL_POLYGON				0x0009

#define GL_STENCIL_TEST			0x0B90
#define GL_KEEP				0x1E00
#define GL_REPLACE				0x1E01
#define GL_INCR				0x1E02
#define GL_DECR				0x1E03

#define GL_FALSE				0x0
#define GL_TRUE				0x1

#define GL_TEXTURE_1D			0x0DE0
#define GL_TEXTURE_2D			0x0DE1
#define GL_TEXTURE_2D_MULTISAMPLE			0x9100
#define GL_TEXTURE_3D			0x806F
#define GL_TEXTURE_CUBE_MAP_ARB		0x8513
#define GL_TEXTURE_2D_ARRAY_EXT		0x8C1A
#define GL_TEXTURE_RECTANGLE_EXT		0x84F5
#define GL_CULL_FACE			0x0B44
#define GL_NONE				0
#define GL_FRONT_LEFT			0x0400
#define GL_FRONT_RIGHT			0x0401
#define GL_BACK_LEFT			0x0402
#define GL_BACK_RIGHT			0x0403
#define GL_FRONT				0x0404
#define GL_BACK				0x0405
#define GL_LEFT				0x0406
#define GL_RIGHT				0x0407
#define GL_FRONT_AND_BACK			0x0408
#define GL_AUX0				0x0409
#define GL_AUX1				0x040A
#define GL_AUX2				0x040B
#define GL_AUX3				0x040C
#define GL_POLYGON_OFFSET_FACTOR		0x8038
#define GL_POLYGON_OFFSET_UNITS			0x2A00
#define GL_POLYGON_OFFSET_POINT			0x2A01
#define GL_POLYGON_OFFSET_LINE			0x2A02
#define GL_POLYGON_OFFSET_FILL			0x8037
#define GL_HALF_FLOAT_ARB			0x140B
#define GL_TEXTURE0_ARB			0x84C0
#define GL_TEXTURE1_ARB			0x84C1
#define GL_TEXTURE2_ARB                   	0x84C2

// texture gen parameter
#define GL_TEXTURE_GEN_MODE			0x2500
#define GL_OBJECT_PLANE			0x2501
#define GL_EYE_PLANE			0x2502
#define GL_FOG_HINT				0x0C54
#define GL_TEXTURE_GEN_S			0x0C60
#define GL_TEXTURE_GEN_T			0x0C61
#define GL_TEXTURE_GEN_R			0x0C62
#define GL_TEXTURE_GEN_Q			0x0C63

#define GL_VERTEX_ARRAY			0x8074
#define GL_NORMAL_ARRAY			0x8075
#define GL_COLOR_ARRAY			0x8076
#define GL_INDEX_ARRAY			0x8077
#define GL_TEXTURE_COORD_ARRAY		0x8078
#define GL_EDGE_FLAG_ARRAY			0x8079

#define GL_DEPTH_TEXTURE_MODE_ARB		0x884B
#define GL_TEXTURE_COMPARE_MODE_ARB		0x884C
#define GL_TEXTURE_COMPARE_FUNC_ARB		0x884D
#define GL_COMPARE_R_TO_TEXTURE_ARB		0x884E
#define GL_TEXTURE_COMPARE_FAIL_VALUE_ARB	0x80BF

namespace imdraw
{
	enum class EMatrixMode
	{
		ModelView,
		Projection,
		Texture // unused
	};

	using Topology = skygfx::utils::MeshBuilder::Mode;

	constexpr auto GL_SMOOTH = 0x1D01;
	constexpr auto GL_FLAT = 0x1D00;

	constexpr auto GL_BGRA = 0x80E1;
	constexpr auto GL_RGB = 0x1907;
	constexpr auto GL_RGBA = 0x1908;

	constexpr auto GL_SCISSOR_TEST = 0x0C11;

	constexpr auto GL_FOG = 0x0B60;
	constexpr auto GL_DEPTH_BUFFER_BIT = 0x00000100;
	constexpr auto GL_STENCIL_BUFFER_BIT = 0x00000400;

	constexpr auto GL_FOG_COLOR = 0x0B66;
	constexpr auto GL_FOG_MODE = 0x0B65;
	constexpr auto GL_FOG_START = 0x0B63;
	constexpr auto GL_FOG_END = 0x0B64;
	constexpr auto GL_FOG_DENSITY = 0x0B62;

	constexpr auto GL_EXP = 0x0800;
	constexpr auto GL_EXP2 = 0x0801;

	constexpr auto GL_COLOR_BUFFER_BIT = 0x00004000;

//	constexpr auto GL_TEXTURE = 0x1702;
	constexpr auto GL_S = 0x2000;
	constexpr auto GL_ENABLE_BIT = 0x00002000;
	constexpr auto GL_T = 0x2001;
	constexpr auto GL_R = 0x2002;
	constexpr auto GL_Q = 0x2003;

	constexpr auto GL_FILL = 0x1B02;

	constexpr auto GL_VENDOR = 0x1F00;
	constexpr auto GL_RENDERER = 0x1F01;
	constexpr auto GL_VERSION = 0x1F02;
	constexpr auto GL_EXTENSIONS = 0x1F03;
	constexpr auto GL_MAX_TEXTURE_SIZE = 0x0D33;
	constexpr auto GL_STACK_OVERFLOW = 0x0503;
	constexpr auto GL_STACK_UNDERFLOW = 0x0504;
	constexpr auto GL_INVALID_ENUM = 0x0500;
	constexpr auto GL_INVALID_VALUE = 0x0501;
	constexpr auto GL_INVALID_OPERATION = 0x0502;
	constexpr auto GL_OUT_OF_MEMORY = 0x0505;
	constexpr auto GL_NO_ERROR = 0;

	constexpr auto GL_DEPTH_TEXTURE_MODE = 0x884B;
	constexpr auto GL_LUMINANCE = 0x1909;
	constexpr auto GL_INTENSITY = 0x8049;
	constexpr auto GL_CLAMP = 0x2900;
	constexpr auto GL_REPEAT = 0x2901;

	constexpr auto GL_RGBA8 = 0x8058;
	constexpr auto GL_RGB8 = 0x8051;
	constexpr auto GL_RGB5 = 0x8050;
	constexpr auto GL_RGBA4 = 0x8056;
	constexpr auto GL_INTENSITY8 = 0x804B;
	constexpr auto GL_LUMINANCE8 = 0x8040;
	constexpr auto GL_LUMINANCE_ALPHA = 0x190A;
	constexpr auto GL_LUMINANCE8_ALPHA8 = 0x8045;
	constexpr auto GL_R8 = 0x8229;
	constexpr auto GL_RG8 = 0x822B;
	constexpr auto GL_R16 = 0x822A;
	constexpr auto GL_RG16 = 0x822C;
	constexpr auto GL_R16F = 0x822D;
	constexpr auto GL_R32F = 0x822E;
	constexpr auto GL_RG16F = 0x822F;
	constexpr auto GL_RG32F = 0x8230;
	constexpr auto GL_RGB16F = 0x881B;
	constexpr auto GL_RGBA16F = 0x881A;
	constexpr auto GL_RGB32F = 0x8815;
	constexpr auto GL_RGBA32F = 0x8814;
	constexpr auto GL_DEPTH_COMPONENT16 = 0x81A5;
	constexpr auto GL_DEPTH_COMPONENT24 = 0x81A6;
	constexpr auto GL_DEPTH_COMPONENT32F = 0x8CAC;

	constexpr auto GL_DEPTH_COMPONENT = 0x1902;
	constexpr auto GL_HALF_FLOAT = 0x140B;
	
	constexpr auto GL_LUMINANCE4_ALPHA4 = 0x8043;
	constexpr auto GL_LUMINANCE4 = 0x803F;
	constexpr auto GL_ALPHA8 = 0x803C;

	constexpr auto GL_LINE = 0x1B01;

	constexpr auto GL_CCW = skygfx::FrontFace::CounterClockwise;
	constexpr auto GL_CW = skygfx::FrontFace::Clockwise;

	extern std::optional<skygfx::BackendType> BackendType;
	extern skygfx::Adapter Adapter;

	void Init();

	void pglClearColor(float red, float green, float blue, float alpha);
	void pglClear(uint32_t mask);

	//void Begin(Topology mode);

	void BeginFrame();
	void EndFrame();

	//void Color4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
	//void Color4ubv(const uint8_t* v);
	//void Color4f(float red, float green, float blue, float alpha);
	//void Color3f(float red, float green, float blue);

	void pglMatrixMode(GLenum mode);
	void pglLoadMatrixf(const float* m);
	void pglLoadIdentity();

	//void DepthFunc(skygfx::ComparisonFunc func);
	void pglDepthRange(float zNear, float zFar);

	//void BlendFunc(skygfx::Blend sfactor, skygfx::Blend dfactor);
	//void ColorMask(bool red, bool green, bool blue, bool alpha); // TODO: SetColorMask
	//void AlphaFunc(skygfx::ComparisonFunc func, float ref);
	
	//void CullFace(skygfx::CullMode mode);
	void pglFrontFace(skygfx::FrontFace mode);

	//void Enable(uint32_t cap);
	//void Disable(uint32_t cap);
	bool pglIsEnabled(uint32_t cap);

	const uint8_t* pglGetString(uint32_t name);
	void pglGetIntegerv(uint32_t pname, int32_t* params);
	uint32_t pglGetError();
	void pglGetFloatv(uint32_t pname, float* params);

	void pglViewport(int32_t x, int32_t y, int32_t width, int32_t height);
	void pglOrtho(float left, float right, float bottom, float top, float zNear, float zFar);
	void pglClipPlane(uint32_t plane, const double* equation);
	void pglHint(uint32_t target, uint32_t mode);
	void pglDrawBuffer(uint32_t mode);
	void pglFinish(); // TODO: maybe dont need
	void pglReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void* pixels);
	void pglPolygonMode(uint32_t face, uint32_t mode);
	void pglPolygonOffset(float factor, float units);
	void pglLineWidth(float width);

	void pglFogi(uint32_t pname, int32_t param);
	void pglFogf(uint32_t pname, float param);
	void pglFogfv(uint32_t pname, const float* params);

	//void StencilFunc(skygfx::ComparisonFunc func, int32_t ref, uint32_t mask);
	//void StencilOp(skygfx::StencilOp fail, skygfx::StencilOp zfail, skygfx::StencilOp zpass);

	void pglBindTexture(GLenum target, GLuint texture);
	void pglGenTextures(GLsizei n, GLuint* textures);
	void pglDeleteTextures(GLsizei n, const GLuint* textures);
	void pglTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset, int32_t width, int32_t height, uint32_t format, uint32_t type, const void* pixels);
	void pglTexImage2D(uint32_t target, int32_t level, int32_t internalformat, int32_t width, int32_t height, int32_t border, uint32_t format, uint32_t type, const void* pixels);

	void pglTexParameteri(GLenum target, GLenum pname, GLint param);

	bool IsAlphaTestEnabled();
	//void SetAlphaTestEnabled(bool value);

	//bool IsBlendEnabled();
	//void SetBlendEnabled(bool value);

	bool IsDepthBiasEnabled();
	//void SetDepthBiasEnabled(bool value);

	bool IsDepthTestEnabled();
	//void SetDepthTestEnabled(bool value);

	bool IsStencilEnabled();
	//void SetStencilEnabled(bool value);

	bool IsCullEnabled();
	//void SetCullEnabled(bool value);

	// pgl

	void pglColor4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
	void pglColor4ubv(const uint8_t* v);
	void pglColor4f(float red, float green, float blue, float alpha);
	void pglColor3f(float red, float green, float blue);

	void pglEnable(uint32_t cap);
	void pglDisable(uint32_t cap);
	void pglBlendFunc(uint32_t sfactor, uint32_t dfactor);
	void pglDepthFunc(uint32_t func);
	void pglAlphaFunc(uint32_t func, float ref);
	void pglTexEnvf(uint32_t target, uint32_t pname, float param);
	void pglTexEnvi(uint32_t target, uint32_t pname, int32_t param);
	void pglTranslatef(float x, float y, float z);
	void pglScalef(float x, float y, float z);
	void pglEnd();
	void pglPointSize(float size);
	void pglVertex2f(float x, float y);
	void pglVertex3fv(const float* v);
	void pglVertex3f(float x, float y, float z);
	void pglTexCoord2f(float s, float t);
	void pglNormal3fv(const float* v);
	void pglBegin(uint32_t mode);
	void pglShadeModel(uint32_t mode);
	void pglStencilFunc(uint32_t func, int32_t ref, uint32_t mask);
	void pglColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
	void pglStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass);
	void pglStencilMask(uint32_t mask);
	void pglDepthMask(bool enabled);
	void pglCullFace(GLenum mode);

	void pglActiveTextureARB(GLenum);
	void pglClientActiveTextureARB(GLenum);
	void pglMultiTexCoord2f(GLenum, GLfloat, GLfloat);
	void pglTexGeni(GLenum coord, GLenum pname, GLint param);
	void pglDisableClientState(GLenum array);
	void pglEnableClientState(GLenum array);
	GLboolean pglIsTexture(GLuint texture);
	void pglTexParameterfv(GLenum target, GLenum pname, const GLfloat* params);
	void pglTexParameterf(GLenum target, GLenum pname, GLfloat param);
	void pglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);
	void pglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);
	void pglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);
	void pglDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid* indices);
	void pglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);
	void pglTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid* pixels);
	void pglTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* pixels);
	void pglTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid* pixels);
	void pglTexImage3D(GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* pixels);
	void pglTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
	void pglGenBuffersARB(GLsizei n, GLuint* buffers);
	void pglBindBufferARB(GLenum target, GLuint buffer);
	void pglBufferDataARB(GLenum target, GLsizeiptrARB size, const GLvoid* data, GLenum usage);
	void pglBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid* data);
	void pglDeleteBuffersARB(GLsizei n, const GLuint* buffers);
	void pglDrawArrays(GLenum mode, GLint first, GLsizei count);
	void pglCompressedTexImage3DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void* data);
	void pglCompressedTexImage2DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void* data);
	void pglCompressedTexImage1DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void* data);
	void pglCompressedTexSubImage3DARB(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void* data);
	void pglCompressedTexSubImage2DARB(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data);
	void pglCompressedTexSubImage1DARB(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void* data);
	void pglDebugMessageCallbackARB(std::any callback, void* userParam);
	void pglDebugMessageControlARB(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled);

	skygfx::Texture* GetTextureByIndex(int index);
}