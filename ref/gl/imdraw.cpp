#include "imdraw.h"

std::optional<skygfx::BackendType> imdraw::BackendType = skygfx::BackendType::OpenGL;
skygfx::Adapter imdraw::Adapter = skygfx::Adapter::HighPerformance;

//#define USE_SKYGFX_TEXTURE_READING

struct SampledTexture
{
	SampledTexture(skygfx::Texture&& _texture) :
		texture(std::move(_texture))
	{
	}

	skygfx::Texture texture;
	skygfx::Sampler sampler = skygfx::Sampler::Nearest;
	skygfx::TextureAddress texaddr = skygfx::TextureAddress::Wrap;
#ifndef USE_SKYGFX_TEXTURE_READING
	std::unordered_map<uint32_t, std::vector<uint8_t>> mip_pixels;
#endif
	float mipmap_bias = 0.0f;
};

static glm::vec4 gClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
static bool gBegined = false;
static imdraw::EMatrixMode gMatrixMode = imdraw::EMatrixMode::Projection;
static std::unordered_map<imdraw::EMatrixMode, glm::mat4> gMatrices;

using TextureId = int;

static std::unordered_map<TextureId, SampledTexture> gTextures;

static uint32_t mTexturesIndexCount;

static std::unordered_map<uint32_t, TextureId> gCurrentTexture;

static skygfx::utils::Scratch::State gState;
static skygfx::utils::Scratch gScratch;

static bool gIsBlendEnabled = true;
static skygfx::BlendMode gBlendMode = skygfx::BlendStates::NonPremultiplied;

static bool gIsDepthTestEnabled = false;
static skygfx::DepthMode gDepthMode;

static bool gStencilEnabled = false;
static skygfx::StencilMode gStencilMode;

static bool gDepthBiasEnabled = false;
static skygfx::DepthBias gDepthBias;

static bool gCullEnabled = false;
static skygfx::CullMode gCullMode = skygfx::CullMode::None;

static bool gAlphaTestEnabled = false;
static float gAlphaTestThreshold = 0.0f;

static uint32_t gActiveTexture = 0;

static skygfx::utils::Mesh::Vertex gVertex;

void imdraw::Init()
{
	skygfx::SetAnisotropyLevel(skygfx::AnisotropyLevel::X16);
}

void imdraw::pglClearColor(float red, float green, float blue, float alpha)
{
	gClearColor = { red, green, blue, alpha };
}

void imdraw::pglClear(uint32_t mask)
{
	skygfx::Clear(gClearColor);
}

static void FillState()
{
	const auto& sampled_texture = gTextures.at(gCurrentTexture.at(gActiveTexture));

	gState.texture = (skygfx::Texture*)&sampled_texture.texture;
	gState.sampler = sampled_texture.sampler;
	gState.texaddr = sampled_texture.texaddr;
	gState.mipmap_bias = sampled_texture.mipmap_bias;

	gState.projection_matrix = gMatrices.at(imdraw::EMatrixMode::Projection);
	gState.view_matrix = gMatrices.at(imdraw::EMatrixMode::ModelView);

	gState.blend_mode = gIsBlendEnabled ? std::make_optional(gBlendMode) : std::nullopt;
	gState.depth_mode = gIsDepthTestEnabled ? std::make_optional(gDepthMode) : std::nullopt;
	gState.stencil_mode = gStencilEnabled ? std::make_optional(gStencilMode) : std::nullopt;
	gState.depth_bias = gDepthBiasEnabled ? std::make_optional(gDepthBias) : std::nullopt;
	gState.cull_mode = gCullEnabled ? gCullMode : skygfx::CullMode::None;
	gState.alpha_test_threshold = gAlphaTestEnabled ? std::make_optional(gAlphaTestThreshold) : std::nullopt;
}

static void Begin(skygfx::utils::MeshBuilder::Mode mode)
{
	assert(!gBegined);
	gBegined = true;

	FillState();
	gScratch.begin(mode, gState);
}

//static skygfx::RenderTarget* gTarget;

void imdraw::BeginFrame()
{
	assert(!gBegined);

//	gTarget = skygfx::AcquireTransientRenderTarget();
//	skygfx::SetRenderTarget(*gTarget);
}

void imdraw::EndFrame()
{
	assert(!gBegined);
	gScratch.flush();

//	skygfx::utils::passes::Bloom(gTarget, nullptr, 0.9f);

//	skygfx::utils::passes::Blit(gTarget, nullptr, {
//		.effect = skygfx::utils::effects::GammaCorrection{
//			.gamma = 1.2f
//		}
//	});
}

static void Color4f(float red, float green, float blue, float alpha)
{
	gVertex.color = glm::vec4{ red, green, blue, alpha };
}

static void Color3f(float red, float green, float blue)
{
	Color4f(red, green, blue, gVertex.color.a);
}

static void Color4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
	Color4f(
		float(red) / 255.0f,
		float(green) / 255.0f,
		float(blue) / 255.0f,
		float(alpha) / 255.0f
	);
}

static void Color4ubv(const uint8_t* v)
{
	Color4ub(v[0], v[1], v[2], v[3]);
}

bool imdraw::pglIsEnabled(uint32_t cap)
{
	if (cap == GL_FOG)
		return false;

	assert(false);
	return false;
}

static void AlphaFunc(skygfx::ComparisonFunc func, float ref)
{
	assert(!gBegined);
	gAlphaTestThreshold = ref;
}

static void BlendFunc(skygfx::Blend sfactor, skygfx::Blend dfactor)
{
	assert(!gBegined);
	auto prev_color_mask = gBlendMode.color_mask;
	gBlendMode = skygfx::BlendMode(sfactor, dfactor);
	gBlendMode.color_mask = prev_color_mask;
}

void imdraw::pglViewport(int32_t x, int32_t y, int32_t width, int32_t height)
{
	assert(!gBegined);
	gState.viewport = skygfx::Viewport{ { (float)x, (float)y }, { (float)width, (float)height } };
}

void imdraw::pglMatrixMode(GLenum mode)
{
	static const std::unordered_map<GLenum, EMatrixMode> MatrixModeMap = {
		{ GL_PROJECTION, EMatrixMode::Projection },
		{ GL_MODELVIEW, EMatrixMode::ModelView },
		{ GL_TEXTURE, EMatrixMode::Texture },
	};

	assert(!gBegined);
	gMatrixMode = MatrixModeMap.at(mode);
}

void imdraw::pglLoadMatrixf(const float* m)
{
	assert(!gBegined);
	gMatrices[gMatrixMode] = *(glm::mat4*)m;
}

void imdraw::pglLoadIdentity()
{
	assert(!gBegined);
	gMatrices[gMatrixMode] = glm::mat4(1.0f);
}

void imdraw::pglOrtho(float left, float right, float bottom, float top, float zNear, float zFar)
{
	assert(!gBegined);
	auto width = right - left;
	auto height = bottom - top;
	auto [proj, view] = skygfx::utils::MakeCameraMatrices(skygfx::utils::OrthogonalCamera{
		.width = width,
		.height = height
	});
	gMatrices[imdraw::EMatrixMode::Projection] = proj;
	gMatrices[imdraw::EMatrixMode::ModelView] = view;
}

static void DepthFunc(skygfx::ComparisonFunc func)
{
	assert(!gBegined);
	gDepthMode = func;
}

void imdraw::pglDepthRange(float zNear, float zFar)
{
	assert(!gBegined);
}

void imdraw::pglClipPlane(uint32_t plane, const double* equation)
{
	assert(!gBegined);
}

void imdraw::pglGetFloatv(uint32_t pname, float* params)
{
	assert(!gBegined);

	if (pname == GL_MAX_TEXTURE_LOD_BIAS_EXT)
	{
		params[0] = 10.0f;
	}
	else
	{
		assert(false);
	}
}

void imdraw::pglFogi(uint32_t pname, int32_t param)
{
	assert(!gBegined);
}

void imdraw::pglFogf(uint32_t pname, float param)
{
	assert(!gBegined);
}

void imdraw::pglFogfv(uint32_t pname, const float* params)
{
	assert(!gBegined);
}

void imdraw::pglHint(uint32_t target, uint32_t mode)
{
	assert(!gBegined);
}

void imdraw::pglDrawBuffer(uint32_t mode)
{
	assert(!gBegined);
}

void imdraw::pglFinish()
{
	assert(!gBegined);
}

static void CullFace(skygfx::CullMode mode)
{
	assert(!gBegined);
	gCullMode = mode;
}

void imdraw::pglReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void* pixels)
{
	assert(!gBegined);
}

static void StencilFunc(skygfx::ComparisonFunc func, int32_t ref, uint32_t mask)
{
	assert(!gBegined);
	gStencilMode.func = func;
	gStencilMode.reference = (uint8_t)ref;
	gStencilMode.read_mask = (uint8_t)mask;
}

static void StencilOp(skygfx::StencilOp fail, skygfx::StencilOp zfail, skygfx::StencilOp zpass)
{
	assert(!gBegined);
	gStencilMode.fail_op = fail;
	gStencilMode.depth_fail_op = zfail;
	gStencilMode.pass_op = zpass;
}

void imdraw::pglPolygonMode(uint32_t face, uint32_t mode)
{
	assert(!gBegined);
}

void imdraw::pglPolygonOffset(float factor, float units)
{
	assert(!gBegined);
	gDepthBias.factor = factor;
	gDepthBias.units = units;
}

void imdraw::pglFrontFace(skygfx::FrontFace mode)
{
	assert(!gBegined);
	gState.front_face = mode;
}

void imdraw::pglLineWidth(float width)
{
	assert(!gBegined);
}

const uint8_t* imdraw::pglGetString(uint32_t name)
{
	if (name == GL_VENDOR)
	{
		return (uint8_t*)"vendor";
	}
	else if (name == GL_RENDERER)
	{
		return (uint8_t*)"skygfx";
	}
	else if (name == GL_EXTENSIONS)
	{
		return (uint8_t*)
		//	"GL_ARB_vertex_buffer_object\n"
		//	"GL_ARB_multitexture\n"
			"GL_EXT_texture_lod_bias\n"
			;
	}
	else if (name == GL_VERSION)
	{
		return (uint8_t*)"1";
	}

	assert(false);
	return (uint8_t*)"";
}

void imdraw::pglGetIntegerv(uint32_t pname, int32_t* params)
{
	if (pname == GL_MAX_TEXTURE_SIZE)
	{
		params[0] = 32768;
		return;
	}
	else if (pname == GL_MAX_TEXTURE_UNITS_ARB)
	{
		params[0] = 3;
		return;
	}
	assert(false);
}

uint32_t imdraw::pglGetError()
{
	return GL_NO_ERROR;
}

void imdraw::pglTexParameteri(GLenum target, GLenum pname, GLint param)
{
	assert(!gBegined);
	assert(target == GL_TEXTURE_2D);

	auto& sampled_texture = gTextures.at(gCurrentTexture.at(gActiveTexture));

	if (pname == GL_TEXTURE_MIN_FILTER && (param == GL_NEAREST_MIPMAP_NEAREST || param == GL_NEAREST))
	{
		sampled_texture.sampler = skygfx::Sampler::Nearest;
	}
	else if (pname == GL_TEXTURE_MIN_FILTER && (param == GL_LINEAR_MIPMAP_LINEAR || param == GL_LINEAR))
	{
		sampled_texture.sampler = skygfx::Sampler::Linear;
	}
	else if (pname == GL_TEXTURE_MAG_FILTER && param == GL_NEAREST)
	{
		// nothing
	}
	else if (pname == GL_TEXTURE_MAG_FILTER && param == GL_LINEAR)
	{
		// nothing
	}
	else if (pname == GL_TEXTURE_WRAP_S && param == GL_CLAMP)
	{
		sampled_texture.texaddr = skygfx::TextureAddress::Clamp;
	}
	else if (pname == GL_TEXTURE_WRAP_S && param == GL_REPEAT)
	{
		sampled_texture.texaddr = skygfx::TextureAddress::Wrap;
	}
	else if (pname == GL_TEXTURE_WRAP_T && param == GL_CLAMP)
	{
		// nothing
	}
	else if (pname == GL_TEXTURE_WRAP_T && param == GL_REPEAT)
	{
		// nothing
	}
	else
		assert(false);
}

void imdraw::pglBindTexture(GLenum target, GLuint texture)
{
	assert(!gBegined);
	assert(target == GL_TEXTURE_2D);
	gCurrentTexture[gActiveTexture] = texture;
}

void imdraw::pglGenTextures(GLsizei n, GLuint* textures)
{
	assert(!gBegined);
	for (int32_t i = 0; i < n; i++)
	{
		auto index = mTexturesIndexCount;
		textures[i] = index;
		mTexturesIndexCount++;
	}
}

void imdraw::pglDeleteTextures(GLsizei n, const GLuint* textures)
{
	assert(!gBegined);
	for (int32_t i = 0; i < n; i++)
	{
		auto index = textures[i];
		gTextures.erase(index);
	}
}

void imdraw::pglTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset, int32_t width, int32_t height, uint32_t format, uint32_t type, const void* pixels)
{
	assert(!gBegined);
	gScratch.flush();
	auto index = gCurrentTexture.at(gActiveTexture);
	gTextures.at(index).texture.write(width, height, (void*)pixels, level, xoffset, yoffset);
}

void imdraw::pglTexImage2D(uint32_t target, int32_t level, int32_t internalformat, int32_t width, int32_t height, int32_t border, uint32_t format, uint32_t type, const void* pixels)
{
	assert(!gBegined);
	auto index = gCurrentTexture.at(gActiveTexture);

	auto _format = skygfx::PixelFormat::RGBA8UNorm;

	if (!gTextures.contains(index))
	{
		assert(level == 0);
		gTextures.insert({ index, SampledTexture(skygfx::Texture(width, height, _format, level + 1)) });
	}
	else
	{
		auto& base_sampled_texture = gTextures.at(index);
		auto& base_texture = base_sampled_texture.texture;
		if (level >= base_texture.getMipCount())
		{
			auto base_width = base_texture.getWidth();
			auto base_height = base_texture.getHeight();
			auto new_texture = skygfx::Texture(base_width, base_height, _format, level + 1);
			for (uint32_t i = 0; i < base_texture.getMipCount(); i++)
			{
				auto mip_width = skygfx::GetMipWidth(base_width, i);
				auto mip_height = skygfx::GetMipHeight(base_height, i);
#ifndef USE_SKYGFX_TEXTURE_READING
				const auto& mip_pixels = base_sampled_texture.mip_pixels.at(i);
#else
				auto mip_pixels = base_texture.read(i);
#endif
				new_texture.write(mip_width, mip_height, (void*)mip_pixels.data(), i);
			}
			base_texture = std::move(new_texture);
		}
	}

	if (pixels == nullptr)
		return;

	auto& sampled_texture = gTextures.at(index);
#ifndef USE_SKYGFX_TEXTURE_READING
	auto& mip_pixels = sampled_texture.mip_pixels[level];
	mip_pixels.resize(width * height * 4);
	memcpy(mip_pixels.data(), pixels, mip_pixels.size());
#endif
	sampled_texture.texture.write(width, height, (void*)pixels, level);

}

static void ColorMask(bool red, bool green, bool blue, bool alpha)
{
	assert(!gBegined);
	gBlendMode.color_mask.red = red;
	gBlendMode.color_mask.green = green;
	gBlendMode.color_mask.blue = blue;
	gBlendMode.color_mask.alpha = alpha;
}

bool imdraw::IsAlphaTestEnabled()
{
	return gAlphaTestEnabled;
}

static void SetAlphaTestEnabled(bool value)
{
	gAlphaTestEnabled = value;
}

static bool IsBlendEnabled()
{
	return gIsBlendEnabled;
}

static void SetBlendEnabled(bool value)
{
	gIsBlendEnabled = value;
}

bool imdraw::IsDepthBiasEnabled()
{
	return gDepthBiasEnabled;
}

static void SetDepthBiasEnabled(bool value)
{
	gDepthBiasEnabled = value;
}

bool imdraw::IsDepthTestEnabled()
{
	return gIsDepthTestEnabled;
}

static void SetDepthTestEnabled(bool value)
{
	gIsDepthTestEnabled = value;
}

bool imdraw::IsStencilEnabled()
{
	return gStencilEnabled;
}

static void SetStencilEnabled(bool value)
{
	gStencilEnabled = value;
}

bool imdraw::IsCullEnabled()
{
	return gCullEnabled;
}

static void SetCullEnabled(bool value)
{
	gCullEnabled = value;
}

void imdraw::pglColor4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
	Color4ub(red, green, blue, alpha);
}

void imdraw::pglColor4ubv(const uint8_t* v)
{
	Color4ubv(v);
}

void imdraw::pglColor4f(float red, float green, float blue, float alpha)
{
	Color4f(red, green, blue, alpha);
}

void imdraw::pglColor3f(float red, float green, float blue)
{
	Color3f(red, green, blue);
}

void imdraw::pglEnable(uint32_t cap)
{
	assert(!gBegined);

	if (cap == GL_SCISSOR_TEST || cap == GL_TEXTURE_2D || cap == GL_FOG)
	{
		/*nothing*/
	}
	else if (cap == GL_BLEND)
	{
		SetBlendEnabled(true);
	}
	else if (cap == GL_ALPHA_TEST)
	{
		SetAlphaTestEnabled(true);
	}
	else if (cap == GL_DEPTH_TEST)
	{
		SetDepthTestEnabled(true);
	}
	else if (cap == GL_STENCIL_TEST)
	{
		SetStencilEnabled(true);
	}
	else if (cap == GL_CULL_FACE)
	{
		SetCullEnabled(true);
	}
	else if (cap == GL_POLYGON_OFFSET_FILL)
	{
		SetDepthBiasEnabled(true);
	}
	else
		assert(false);
}

void imdraw::pglDisable(uint32_t cap)
{
	assert(!gBegined);

	if (cap == GL_SCISSOR_TEST || cap == GL_TEXTURE_2D || cap == imdraw::GL_FOG)
	{
		/*nothing*/
	}
	else if (cap == GL_BLEND)
	{
		SetBlendEnabled(false);
	}
	else if (cap == GL_ALPHA_TEST)
	{
		SetAlphaTestEnabled(false);
	}
	else if (cap == GL_DEPTH_TEST)
	{
		SetDepthTestEnabled(false);
	}
	else if (cap == GL_STENCIL_TEST)
	{
		SetStencilEnabled(false);
	}
	else if (cap == GL_CULL_FACE)
	{
		SetCullEnabled(false);
	}
	else if (cap == GL_POLYGON_OFFSET_FILL)
	{
		SetDepthBiasEnabled(false);
	}
	else
		assert(false);
}

void imdraw::pglBlendFunc(uint32_t sfactor, uint32_t dfactor)
{
	const static std::unordered_map<uint32_t, skygfx::Blend> BlendMap = {
		{ GL_ONE, skygfx::Blend::One },
		{ GL_ZERO, skygfx::Blend::Zero, },
		{ GL_SRC_COLOR, skygfx::Blend::SrcColor },
		{ GL_ONE_MINUS_SRC_COLOR, skygfx::Blend::InvSrcColor },
		{ GL_SRC_ALPHA, skygfx::Blend::SrcAlpha },
		{ GL_ONE_MINUS_SRC_ALPHA, skygfx::Blend::InvSrcAlpha },
		{ GL_DST_COLOR, skygfx::Blend::DstColor },
		{ GL_ONE_MINUS_DST_COLOR, skygfx::Blend::InvDstColor },
		{ GL_DST_ALPHA, skygfx::Blend::DstAlpha },
		{ GL_ONE_MINUS_DST_ALPHA, skygfx::Blend::InvDstAlpha }
	};

	auto sf = BlendMap.at(sfactor);
	auto df = BlendMap.at(dfactor);

	BlendFunc(sf, df);
}

static const std::unordered_map<uint32_t, skygfx::ComparisonFunc> ComparisonFuncMap = {
	{ GL_ALWAYS, skygfx::ComparisonFunc::Always },
	{ GL_NEVER, skygfx::ComparisonFunc::Never },
	{ GL_LESS, skygfx::ComparisonFunc::Less },
	{ GL_EQUAL, skygfx::ComparisonFunc::Equal },
	{ GL_NOTEQUAL, skygfx::ComparisonFunc::NotEqual },
	{ GL_LEQUAL, skygfx::ComparisonFunc::LessEqual },
	{ GL_GREATER, skygfx::ComparisonFunc::Greater },
	{ GL_GEQUAL, skygfx::ComparisonFunc::GreaterEqual }
};

void imdraw::pglDepthFunc(uint32_t func)
{
	DepthFunc(ComparisonFuncMap.at(func));
}

void imdraw::pglAlphaFunc(uint32_t func, float ref)
{
	AlphaFunc(ComparisonFuncMap.at(func), ref);
}

void imdraw::pglTexEnvf(uint32_t target, uint32_t pname, float param)
{
	// nothing
}

void imdraw::pglTexEnvi(uint32_t target, uint32_t pname, int32_t param)
{
	// nothing
}

void imdraw::pglTranslatef(float x, float y, float z)
{
	assert(!gBegined);
	gMatrices[gMatrixMode] = glm::translate(gMatrices.at(gMatrixMode), { x, y, z });
}

void imdraw::pglScalef(float x, float y, float z)
{
	assert(!gBegined);
	gMatrices[gMatrixMode] = glm::scale(gMatrices.at(gMatrixMode), { x, y, z });
}

void imdraw::pglEnd()
{
	assert(gBegined);
	gBegined = false;
	gScratch.end();
}

void imdraw::pglPointSize(float size)
{
	assert(!gBegined);
	// nothing
}

void imdraw::pglVertex2f(float x, float y)
{
	pglVertex3f(x, y, 0.0f);
}

void imdraw::pglVertex3fv(const float* v)
{
	pglVertex3f(v[0], v[1], v[2]);
}

void imdraw::pglVertex3f(float x, float y, float z)
{
	assert(gBegined);
	gVertex.pos = { x, y, z };
	gScratch.vertex(gVertex);
}

void imdraw::pglTexCoord2f(float s, float t)
{
	assert(gBegined);
	gVertex.texcoord = { s, t };
}

void imdraw::pglNormal3fv(const float* v)
{
	assert(gBegined);
	gVertex.normal = *(glm::vec3*)v;
}

void imdraw::pglBegin(uint32_t mode)
{
	static const std::unordered_map<uint32_t, skygfx::utils::MeshBuilder::Mode> ModeMap = {
		{ GL_POINTS, skygfx::utils::MeshBuilder::Mode::Points },
		{ GL_LINES, skygfx::utils::MeshBuilder::Mode::Lines },
		{ GL_LINE_LOOP, skygfx::utils::MeshBuilder::Mode::LineLoop },
		{ GL_LINE_STRIP, skygfx::utils::MeshBuilder::Mode::LineStrip },
		{ GL_TRIANGLES, skygfx::utils::MeshBuilder::Mode::Triangles },
		{ GL_TRIANGLE_STRIP, skygfx::utils::MeshBuilder::Mode::TriangleStrip },
		{ GL_TRIANGLE_FAN, skygfx::utils::MeshBuilder::Mode::TriangleFan },
		{ GL_QUADS, skygfx::utils::MeshBuilder::Mode::Quads },
	//	{ GL_QUAD_STRIP, skygfx::utils::MeshBuilder::Mode:: },
		{ GL_POLYGON, skygfx::utils::MeshBuilder::Mode::Polygon },
	};

	Begin(ModeMap.at(mode));
}

void imdraw::pglShadeModel(uint32_t mode)
{
	assert(!gBegined);
	// nothing
}

void imdraw::pglStencilFunc(uint32_t func, int32_t ref, uint32_t mask)
{
	StencilFunc(ComparisonFuncMap.at(func), ref, mask);
}

void imdraw::pglColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
	ColorMask(red, green, blue, alpha);
}

void imdraw::pglStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass)
{
	static const std::unordered_map<uint32_t, skygfx::StencilOp> StencilOpMap = {
		{ GL_KEEP, skygfx::StencilOp::Keep },
		{ GL_ZERO, skygfx::StencilOp::Zero },
		{ GL_REPLACE, skygfx::StencilOp::Replace },
		{ GL_INCR, skygfx::StencilOp::IncrementSaturation },
		{ GL_DECR, skygfx::StencilOp::DecrementSaturation },
	//	{ GL_INVERT, skygfx::StencilOp::Invert },
	//	{ GL_INCR_WRAP, skygfx::StencilOp::Increment },
	//	{ GL_DECR_WRAP, skygfx::StencilOp::Decrement },
	};

	StencilOp(StencilOpMap.at(fail), StencilOpMap.at(zfail), StencilOpMap.at(zpass));
}

void imdraw::pglStencilMask(uint32_t mask)
{
	assert(!gBegined);
	gStencilMode.write_mask = (uint8_t)mask;
}

void imdraw::pglDepthMask(bool enabled)
{
	assert(!gBegined);
	gDepthMode.write_mask = enabled;
}

void imdraw::pglCullFace(GLenum mode)
{
	static const std::unordered_map<GLenum, skygfx::CullMode> CullModeMap = {
		{ GL_NONE, skygfx::CullMode::None },
		{ GL_BACK, skygfx::CullMode::Back },
		{ GL_FRONT, skygfx::CullMode::Front },
	};

	CullFace(CullModeMap.at(mode));
}

void imdraw::pglActiveTextureARB(GLenum value)
{
	gActiveTexture = value - GL_TEXTURE0_ARB;
}

void imdraw::pglClientActiveTextureARB(GLenum value)
{
	assert(false);
}

void imdraw::pglMultiTexCoord2f(GLenum, GLfloat, GLfloat)
{
	assert(false);
}

void imdraw::pglTexGeni(GLenum coord, GLenum pname, GLint param)
{
	assert(false);
}

void imdraw::pglDisableClientState(GLenum array)
{
	assert(false);
}

void imdraw::pglEnableClientState(GLenum array)
{
	assert(false);
}

GLboolean imdraw::pglIsTexture(GLuint texture)
{
	assert(false);
	return false;
}

void imdraw::pglTexParameterfv(GLenum target, GLenum pname, const GLfloat* params)
{
	assert(false);
}

void imdraw::pglTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	assert(!gBegined);
	assert(target == GL_TEXTURE_2D);

	auto& sampled_texture = gTextures.at(gCurrentTexture.at(gActiveTexture));

	if (pname == GL_TEXTURE_LOD_BIAS_EXT)
	{
		sampled_texture.mipmap_bias = param;
	}
	else
	{
		assert(false);
	}
}

void imdraw::pglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer)
{
	assert(false);
}

void imdraw::pglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer)
{
	assert(false);
}

void imdraw::pglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer)
{
	assert(false);
}

void imdraw::pglDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid* indices)
{
	assert(false);
}

void imdraw::pglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices)
{
	assert(false);
}

void imdraw::pglTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid* pixels)
{
	assert(false);
}

void imdraw::pglTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* pixels)
{
	assert(false);
}

void imdraw::pglTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid* pixels)
{
	assert(false);
}

void imdraw::pglTexImage3D(GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* pixels)
{
	assert(false);
}

void imdraw::pglTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations)
{
	assert(false);
}

void imdraw::pglGenBuffersARB(GLsizei n, GLuint* buffers)
{
	assert(false);
}

void imdraw::pglBindBufferARB(GLenum target, GLuint buffer)
{
	assert(false);
}

void imdraw::pglBufferDataARB(GLenum target, GLsizeiptrARB size, const GLvoid* data, GLenum usage)
{
	assert(false);
}

void imdraw::pglBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid* data)
{
	assert(false);
}

void imdraw::pglDeleteBuffersARB(GLsizei n, const GLuint* buffers)
{
	assert(false);
}

void imdraw::pglDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	assert(false);
}

void imdraw::pglCompressedTexImage3DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void* data)
{
	assert(false);
}

void imdraw::pglCompressedTexImage2DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void* data)
{
	assert(false);
}

void imdraw::pglCompressedTexImage1DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void* data)
{
	assert(false);
}

void imdraw::pglCompressedTexSubImage3DARB(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void* data)
{
	assert(false);
}

void imdraw::pglCompressedTexSubImage2DARB(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data)
{
	assert(false);
}

void imdraw::pglCompressedTexSubImage1DARB(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void* data)
{
	assert(false);
}

void imdraw::pglDebugMessageCallbackARB(std::any callback, void* userParam)
{
	// nothing
}

void imdraw::pglDebugMessageControlARB(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled)
{
	// nothing
}

skygfx::Texture* imdraw::GetTextureByIndex(int index)
{
	return &gTextures.at(index).texture;
}
