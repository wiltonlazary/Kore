#include "pch.h"

#include "OpenGL.h"
#include "VertexBufferImpl.h"
#include "ogl.h"

#include <Kore/Log.h>
#include <Kore/Math/Core.h>
#include <Kore/System.h>
#include <cstdio>

#if defined(SYS_IOS)
#include <OpenGLES/ES2/glext.h>
#endif

#ifdef SYS_WINDOWS
#include <GL/wglew.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#endif

using namespace Kore;

namespace Kore {
#if !defined(SYS_IOS) && !defined(SYS_ANDROID)
	extern bool programUsesTessellation;
#endif
}

namespace {
#ifdef SYS_WINDOWS
	HINSTANCE instance = 0;
	HDC deviceContexts[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	HGLRC glContexts[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
#endif

	Graphics4::TextureFilter minFilters[10][32];
	Graphics4::MipmapFilter mipFilters[10][32];
	int originalFramebuffer[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	uint arrayId[10];

	int _width;
	int _height;
	int _renderTargetWidth;
	int _renderTargetHeight;
	bool renderToBackbuffer;

	bool depthTest = false;
	bool depthMask = true;

#if defined(OPENGLES) && defined(SYS_ANDROID) && SYS_ANDROID_API >= 18
	void* glesDrawBuffers;
#endif
}

void Graphics4::destroy(int windowId) {
#ifdef SYS_WINDOWS
	if (glContexts[windowId]) {
		if (!wglMakeCurrent(nullptr, nullptr)) {
			// MessageBox(NULL,"Release Of DC And RC Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}
		if (!wglDeleteContext(glContexts[windowId])) {
			// MessageBox(NULL,"Release Rendering Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}
		glContexts[windowId] = nullptr;
	}

	HWND windowHandle = (HWND)System::windowHandle(windowId);

	// TODO (DK) shouldn't 'deviceContexts[windowId] = nullptr;' be moved out of here?
	if (deviceContexts[windowId] && !ReleaseDC(windowHandle, deviceContexts[windowId])) {
		// MessageBox(NULL,"Release Device Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		deviceContexts[windowId] = nullptr;
	}
#endif

	System::destroyWindow(windowId);
}

#undef CreateWindow

#if defined(SYS_WINDOWS)
namespace Kore {
	namespace System {
		extern int currentDeviceId;
	}
}
#endif

#if defined(SYS_WINDOWS)
void Graphics4::setup() {}
#endif

void Graphics4::init(int windowId, int depthBufferBits, int stencilBufferBits, bool vsync) {
#ifdef SYS_WINDOWS
	HWND windowHandle = (HWND)System::windowHandle(windowId);

#ifndef VR_RIFT
	// TODO (DK) use provided settings for depth/stencil buffer

	PIXELFORMATDESCRIPTOR pfd = // pfd Tells Windows How We Want Things To Be
	{
	    sizeof(PIXELFORMATDESCRIPTOR), // Size Of This Pixel Format Descriptor
	    1,                             // Version Number
	    PFD_DRAW_TO_WINDOW |           // Format Must Support Window
	        PFD_SUPPORT_OPENGL |       // Format Must Support OpenGL
	        PFD_DOUBLEBUFFER,          // Must Support Double Buffering
	    PFD_TYPE_RGBA,                 // Request An RGBA Format
	    32,                            // Select Our Color Depth
	    0,
	    0, 0, 0, 0, 0,     // Color Bits Ignored
	    0,                 // No Alpha Buffer
	    0,                 // Shift Bit Ignored
	    0,                 // No Accumulation Buffer
	    0, 0, 0, 0,        // Accumulation Bits Ignored
	    static_cast<BYTE>(depthBufferBits),   // 16Bit Z-Buffer (Depth Buffer)
	    static_cast<BYTE>(stencilBufferBits), // 8Bit Stencil Buffer
	    0,                 // No Auxiliary Buffer
	    PFD_MAIN_PLANE,    // Main Drawing Layer
	    0,                 // Reserved
	    0, 0, 0            // Layer Masks Ignored
	};

	deviceContexts[windowId] = GetDC(windowHandle);
	GLuint pixelFormat = ChoosePixelFormat(deviceContexts[windowId], &pfd);
	SetPixelFormat(deviceContexts[windowId], pixelFormat, &pfd);
	HGLRC tempGlContext = wglCreateContext(deviceContexts[windowId]);
	wglMakeCurrent(deviceContexts[windowId], tempGlContext);
	Kore::System::currentDeviceId = windowId;

	// TODO (DK) make a Graphics::setup() (called from System::setup()) and call it there only once?
	if (windowId == 0) {
		glewInit();
	}

	if (wglewIsSupported("WGL_ARB_create_context") == 1) {
		int attributes[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
		                    4,
		                    WGL_CONTEXT_MINOR_VERSION_ARB,
		                    2,
		                    WGL_CONTEXT_FLAGS_ARB,
		                    WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
		                    WGL_CONTEXT_PROFILE_MASK_ARB,
		                    WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		                    0};

		glContexts[windowId] = wglCreateContextAttribsARB(deviceContexts[windowId], glContexts[0], attributes);
		glCheckErrors();
		wglMakeCurrent(nullptr, nullptr);
		wglDeleteContext(tempGlContext);
		wglMakeCurrent(deviceContexts[windowId], glContexts[windowId]);
		glCheckErrors();
	}
	else {
		glContexts[windowId] = tempGlContext;
	}

	ShowWindow(windowHandle, SW_SHOW);
	SetForegroundWindow(windowHandle); // Slightly Higher Priority
	SetFocus(windowHandle);            // Sets Keyboard Focus To The Window
#else  /* #ifndef VR_RIFT */
	deviceContexts[windowId] = GetDC(windowHandle);
	glContexts[windowId] = wglGetCurrentContext();
	glewInit();
#endif /* #ifndef VR_RIFT */
#endif /* #ifdef SYS_WINDOWS */

#ifndef VR_RIFT
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	setRenderState(DepthTest, false);
	glViewport(0, 0, System::windowWidth(windowId), System::windowHeight(windowId));
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &originalFramebuffer[windowId]);

	for (int i = 0; i < 32; ++i) {
		minFilters[windowId][i] = LinearFilter;
		mipFilters[windowId][i] = NoMipFilter;
	}
#endif

#ifdef SYS_WINDOWS
	if (windowId == 0) {
		if (wglSwapIntervalEXT != nullptr) wglSwapIntervalEXT(vsync);
	}
#endif

#if defined(SYS_IOS)
	glGenVertexArraysOES(1, &arrayId[windowId]);
	glCheckErrors();
#elif !defined(SYS_ANDROID) && !defined(SYS_HTML5) && !defined(SYS_TIZEN) && !defined(SYS_PI)
	glGenVertexArrays(1, &arrayId[windowId]);
	glCheckErrors();
#endif

	_width = System::windowWidth(0);
	_height = System::windowHeight(0);
	_renderTargetWidth = _width;
	_renderTargetHeight = _height;
	renderToBackbuffer = true;

#if defined(OPENGLES) && defined(SYS_ANDROID) && SYS_ANDROID_API >= 18
	glesDrawBuffers = (void*)eglGetProcAddress("glDrawBuffers");
#endif
}

void Graphics4::changeResolution(int width, int height) {
	_width = width;
	_height = height;
	if (renderToBackbuffer) {
		_renderTargetWidth = _width;
		_renderTargetHeight = _height;
	}
}

// TODO (DK) should return displays refreshrate?
unsigned Graphics4::refreshRate() {
	return 60;
}

bool Graphics4::vsynced() {
#ifdef SYS_WINDOWS
	return wglGetSwapIntervalEXT();
#else
	return true;
#endif
}

void Graphics4::setBool(ConstantLocation location, bool value) {
	glUniform1i(location.location, value ? 1 : 0);
	glCheckErrors();
}

void Graphics4::setInt(ConstantLocation location, int value) {
	glUniform1i(location.location, value);
	glCheckErrors();
}

void Graphics4::setFloat(ConstantLocation location, float value) {
	glUniform1f(location.location, value);
	glCheckErrors();
}

void Graphics4::setFloat2(ConstantLocation location, float value1, float value2) {
	glUniform2f(location.location, value1, value2);
	glCheckErrors();
}

void Graphics4::setFloat3(ConstantLocation location, float value1, float value2, float value3) {
	glUniform3f(location.location, value1, value2, value3);
	glCheckErrors();
}

void Graphics4::setFloat4(ConstantLocation location, float value1, float value2, float value3, float value4) {
	glUniform4f(location.location, value1, value2, value3, value4);
	glCheckErrors();
}

void Graphics4::setFloats(ConstantLocation location, float* values, int count) {
	glUniform1fv(location.location, count, values);
	glCheckErrors();
}

void Graphics4::setFloat4s(ConstantLocation location, float* values, int count) {
	glUniform4fv(location.location, count / 4, values);
	glCheckErrors();
}

void Graphics4::setMatrix(ConstantLocation location, const mat4& value) {
	glUniformMatrix4fv(location.location, 1, GL_FALSE, &value.matrix[0][0]);
	glCheckErrors();
}

void Graphics4::setMatrix(ConstantLocation location, const mat3& value) {
	glUniformMatrix3fv(location.location, 1, GL_FALSE, &value.matrix[0][0]);
	glCheckErrors();
}

void Graphics4::drawIndexedVertices() {
	drawIndexedVertices(0, IndexBufferImpl::current->count());
}

void Graphics4::drawIndexedVertices(int start, int count) {
#ifdef OPENGLES
#if defined(SYS_ANDROID) || defined(SYS_PI)
	glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, (void*)(start * sizeof(GL_UNSIGNED_SHORT)));
#else
	glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void*)(start * sizeof(GL_UNSIGNED_INT)));
#endif
	glCheckErrors();
#else
	if (programUsesTessellation) {
		glDrawElements(GL_PATCHES, count, GL_UNSIGNED_INT, (void*)(start * sizeof(GL_UNSIGNED_INT)));
		glCheckErrors();
	}
	else {
		glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void*)(start * sizeof(GL_UNSIGNED_INT)));
		glCheckErrors();
	}
#endif
}

void Graphics4::drawIndexedVerticesInstanced(int instanceCount) {
	drawIndexedVerticesInstanced(instanceCount, 0, IndexBufferImpl::current->count());
}

void Graphics4::drawIndexedVerticesInstanced(int instanceCount, int start, int count) {
#ifndef OPENGLES
	if (programUsesTessellation) {
		glDrawElementsInstanced(GL_PATCHES, count, GL_UNSIGNED_INT, (void*)(start * sizeof(GL_UNSIGNED_INT)), instanceCount);
		glCheckErrors();
	}
	else {
		glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void*)(start * sizeof(GL_UNSIGNED_INT)), instanceCount);
		glCheckErrors();
	}
#endif
}

bool Graphics4::swapBuffers(int contextId) {
#ifdef SYS_WINDOWS
	::SwapBuffers(deviceContexts[contextId]);
#else
	System::swapBuffers(contextId);
#endif
	return true;
}

#ifdef SYS_IOS
void beginGL();
#endif

#if defined(SYS_WINDOWS)
void Graphics4::makeCurrent(int contextId) {
	wglMakeCurrent(deviceContexts[contextId], glContexts[contextId]);
}
#endif

void Graphics4::begin(int contextId) {
	if (System::currentDevice() != -1) {
		if (System::currentDevice() != contextId) {
			log(Warning, "begin: wrong glContext is active");
		}
		else {
			//**log(Warning, "begin: a glContext is still active");
		}

		// return; // TODO (DK) return here?
	}

	// System::setCurrentDevice(contextId);
	System::makeCurrent(contextId);

	glViewport(0, 0, _width, _height);

#ifdef SYS_IOS
	beginGL();
#endif

#ifdef SYS_ANDROID
	// if rendered to a texture, strange things happen if the backbuffer is not cleared
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
#endif
}

void Graphics4::viewport(int x, int y, int width, int height) {
	glViewport(x, _renderTargetHeight - y - height, width, height);
}

void Graphics4::scissor(int x, int y, int width, int height) {
	glEnable(GL_SCISSOR_TEST);
	glScissor(x, _renderTargetHeight - y - height, width, height);
}

void Graphics4::disableScissor() {
	glDisable(GL_SCISSOR_TEST);
}

namespace {
	GLenum convert(Graphics4::StencilAction action) {
		switch (action) {
		default:
		case Graphics4::Decrement:
			return GL_DECR;
		case Graphics4::DecrementWrap:
			return GL_DECR_WRAP;
		case Graphics4::Increment:
			return GL_INCR;
		case Graphics4::IncrementWrap:
			return GL_INCR_WRAP;
		case Graphics4::Invert:
			return GL_INVERT;
		case Graphics4::Keep:
			return GL_KEEP;
		case Graphics4::Replace:
			return GL_REPLACE;
		case Graphics4::Zero:
			return GL_ZERO;
		}
	}
}

void Graphics4::setStencilParameters(ZCompareMode compareMode, StencilAction bothPass, StencilAction depthFail, StencilAction stencilFail, int referenceValue,
                                    int readMask, int writeMask) {
	if (compareMode == ZCompareAlways && bothPass == Keep && depthFail == Keep && stencilFail == Keep) {
		glDisable(GL_STENCIL_TEST);
	}
	else {
		glEnable(GL_STENCIL_TEST);
		int stencilFunc = 0;
		switch (compareMode) {
		case ZCompareAlways:
			stencilFunc = GL_ALWAYS;
			break;
		case ZCompareEqual:
			stencilFunc = GL_EQUAL;
			break;
		case ZCompareGreater:
			stencilFunc = GL_GREATER;
			break;
		case ZCompareGreaterEqual:
			stencilFunc = GL_GEQUAL;
			break;
		case ZCompareLess:
			stencilFunc = GL_LESS;
			break;
		case ZCompareLessEqual:
			stencilFunc = GL_LEQUAL;
			break;
		case ZCompareNever:
			stencilFunc = GL_NEVER;
			break;
		case ZCompareNotEqual:
			stencilFunc = GL_NOTEQUAL;
			break;
		}
		glStencilMask(writeMask);
		glStencilOp(convert(stencilFail), convert(depthFail), convert(bothPass));
		glStencilFunc(stencilFunc, referenceValue, readMask);
	}
}

/*void glCheckErrors() {
    if (System::currentDevice() == -1) {
        log(Warning, "no OpenGL device context is set");
        return;
    }

//#ifdef _DEBUG
    GLenum code = glGetError();
    while (code != GL_NO_ERROR) {
        //std::printf("GLError: %s\n", glewGetErrorString(code));
        switch (code) {
        case GL_INVALID_VALUE:
            log(Warning, "OpenGL: Invalid value");
            break;
        case GL_INVALID_OPERATION:
            log(Warning, "OpenGL: Invalid operation");
            break;
        default:
            log(Warning, "OpenGL: Error code %i", code);
            break;
        }
        code = glGetError();
    }
//#endif
}*/

#if defined(SYS_WINDOWS)
void Graphics4::clearCurrent() {
	wglMakeCurrent(nullptr, nullptr);
}
#endif

// TODO (DK) this never gets called on some targets, needs investigation?
void Graphics4::end(int windowId) {
	// glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
	// glClear(GL_COLOR_BUFFER_BIT);
	glCheckErrors();

	if (System::currentDevice() == -1) {
		log(Warning, "end: a glContext wasn't active");
	}

	if (System::currentDevice() != windowId) {
		log(Warning, "end: wrong glContext is active");
	}

	System::clearCurrent();
}

void Graphics4::clear(uint flags, uint color, float depth, int stencil) {
	glClearColor(((color & 0x00ff0000) >> 16) / 255.0f, ((color & 0x0000ff00) >> 8) / 255.0f, (color & 0x000000ff) / 255.0f,
	             ((color & 0xff000000) >> 24) / 255.0f);
	glCheckErrors();
	if (flags & ClearDepthFlag) {
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glCheckErrors();
	}
#ifdef OPENGLES
	glClearDepthf(depth);
#else
	glClearDepth(depth);
#endif
	glCheckErrors();
	glStencilMask(0xff);
	glCheckErrors();
	glClearStencil(stencil);
	glCheckErrors();
	GLbitfield oglflags = ((flags & ClearColorFlag) ? GL_COLOR_BUFFER_BIT : 0) | ((flags & ClearDepthFlag) ? GL_DEPTH_BUFFER_BIT : 0) |
	                      ((flags & ClearStencilFlag) ? GL_STENCIL_BUFFER_BIT : 0);
	glClear(oglflags);
	glCheckErrors();
	if (depthTest) {
		glEnable(GL_DEPTH_TEST);
	}
	else {
		glDisable(GL_DEPTH_TEST);
	}
	glCheckErrors();
	if (depthMask) {
		glDepthMask(GL_TRUE);
	}
	else {
		glDepthMask(GL_FALSE);
	}
	glCheckErrors();
}

void Graphics4::setColorMask(bool red, bool green, bool blue, bool alpha) {
	glColorMask(red, green, blue, alpha);
}

void Graphics4::setRenderState(RenderState state, bool on) {
	switch (state) {
	case DepthWrite:
		if (on)
			glDepthMask(GL_TRUE);
		else
			glDepthMask(GL_FALSE);
		depthMask = on;
		break;
	case DepthTest:
		if (on)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);
		depthTest = on;
		break;
	case BlendingState:
		if (on)
			glEnable(GL_BLEND);
		else
			glDisable(GL_BLEND);
		break;
	default:
		break;
	}

	glCheckErrors();

	/*switch (state) {
	    case Normalize:
	        device->SetRenderState(D3DRS_NORMALIZENORMALS, on ? TRUE : FALSE);
	        break;
	    case BackfaceCulling:
	        if (on) device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
	        else device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	        break;
	    case FogState:
	        device->SetRenderState(D3DRS_FOGENABLE, on ? TRUE : FALSE);
	        break;
	    case ScissorTestState:
	        device->SetRenderState(D3DRS_SCISSORTESTENABLE, on ? TRUE : FALSE);
	        break;
	    case AlphaTestState:
	        device->SetRenderState(D3DRS_ALPHATESTENABLE, on ? TRUE : FALSE);
	        device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	        break;
	    default:
	        throw Exception();
	}*/
}

void Graphics4::setRenderState(RenderState state, int v) {
	switch (state) {
	case DepthTestCompare:
		switch (v) {
		default:
		case ZCompareAlways:
			v = GL_ALWAYS;
			break;
		case ZCompareNever:
			v = GL_NEVER;
			break;
		case ZCompareEqual:
			v = GL_EQUAL;
			break;
		case ZCompareNotEqual:
			v = GL_NOTEQUAL;
			break;
		case ZCompareLess:
			v = GL_LESS;
			break;
		case ZCompareLessEqual:
			v = GL_LEQUAL;
			break;
		case ZCompareGreater:
			v = GL_GREATER;
			break;
		case ZCompareGreaterEqual:
			v = GL_GEQUAL;
			break;
		}
		glDepthFunc(v);
		glCheckErrors();
		break;
	case BackfaceCulling:
		switch (v) {
		case Clockwise:
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glCheckErrors();
			break;
		case CounterClockwise:
			glEnable(GL_CULL_FACE);
			glCullFace(GL_FRONT);
			glCheckErrors();
			break;
		case NoCulling:
			glDisable(GL_CULL_FACE);
			glCheckErrors();
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	/*switch (state) {
	    case DepthTestCompare:
	        switch (v) {
	                // TODO: Cmp-Konstanten systemabhaengig abgleichen
	            default:
	            case ZCmp_Always      : v = D3DCMP_ALWAYS; break;
	            case ZCmp_Never       : v = D3DCMP_NEVER; break;
	            case ZCmp_Equal       : v = D3DCMP_EQUAL; break;
	            case ZCmp_NotEqual    : v = D3DCMP_NOTEQUAL; break;
	            case ZCmp_Less        : v = D3DCMP_LESS; break;
	            case ZCmp_LessEqual   : v = D3DCMP_LESSEQUAL; break;
	            case ZCmp_Greater     : v = D3DCMP_GREATER; break;
	            case ZCmp_GreaterEqual: v = D3DCMP_GREATEREQUAL; break;
	        }
	        device->SetRenderState(D3DRS_ZFUNC, v);
	        break;
	    case FogTypeState:
	        switch (v) {
	            case LinearFog:
	                device->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
	        }
	        break;
	    case AlphaReferenceState:
	        device->SetRenderState(D3DRS_ALPHAREF, (DWORD)v);
	        break;
	    default:
	        throw Exception();
	}*/
}

void Graphics4::setVertexBuffers(VertexBuffer** vertexBuffers, int count) {
#if defined(SYS_IOS)
	glBindVertexArrayOES(arrayId[0]);
	glCheckErrors();
#elif !defined(SYS_ANDROID) && !defined(SYS_HTML5) && !defined(SYS_TIZEN) && !defined(SYS_PI)
	glBindVertexArray(arrayId[System::currentDevice()]);
	glCheckErrors();
#endif

	int offset = 0;
	for (int i = 0; i < count; ++i) {
		offset += vertexBuffers[i]->_set(offset);
	}
}

void Graphics4::setIndexBuffer(IndexBuffer& indexBuffer) {
	indexBuffer._set();
}

void Graphics4::setTexture(TextureUnit unit, Texture* texture) {
	texture->_set(unit);
}

void Graphics4::setImageTexture(TextureUnit unit, Texture* texture) {
	texture->_setImage(unit);
}

void Graphics4::setTextureAddressing(TextureUnit unit, TexDir dir, TextureAddressing addressing) {
	glActiveTexture(GL_TEXTURE0 + unit.unit);
	GLenum texDir;
	switch (dir) {
	case U:
		texDir = GL_TEXTURE_WRAP_S;
		break;
	case V:
		texDir = GL_TEXTURE_WRAP_T;
		break;
	}
	switch (addressing) {
	case Clamp:
		glTexParameteri(GL_TEXTURE_2D, texDir, GL_CLAMP_TO_EDGE);
		break;
	case Repeat:
		glTexParameteri(GL_TEXTURE_2D, texDir, GL_REPEAT);
		break;
	case Border:
		// unsupported
		glTexParameteri(GL_TEXTURE_2D, texDir, GL_CLAMP_TO_EDGE);
		break;
	case Mirror:
		// unsupported
		glTexParameteri(GL_TEXTURE_2D, texDir, GL_REPEAT);
		break;
	}
	glCheckErrors();
}

void Graphics4::setTextureMagnificationFilter(TextureUnit texunit, TextureFilter filter) {
	glActiveTexture(GL_TEXTURE0 + texunit.unit);
	glCheckErrors();
	switch (filter) {
	case PointFilter:
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		break;
	case LinearFilter:
	case AnisotropicFilter:
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		break;
	}
	glCheckErrors();
}

namespace {
	void setMinMipFilters(int unit) {
		glActiveTexture(GL_TEXTURE0 + unit);
		glCheckErrors();
		switch (minFilters[System::currentDevice()][unit]) {
		case Graphics4::PointFilter:
			switch (mipFilters[System::currentDevice()][unit]) {
			case Graphics4::NoMipFilter:
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				break;
			case Graphics4::PointMipFilter:
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
				break;
			case Graphics4::LinearMipFilter:
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
				break;
			}
			break;
		case Graphics4::LinearFilter:
		case Graphics4::AnisotropicFilter:
			switch (mipFilters[System::currentDevice()][unit]) {
			case Graphics4::NoMipFilter:
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				break;
			case Graphics4::PointMipFilter:
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
				break;
			case Graphics4::LinearMipFilter:
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				break;
			}
			break;
		}
		glCheckErrors();
	}
}

void Graphics4::setTextureMinificationFilter(TextureUnit texunit, TextureFilter filter) {
	minFilters[System::currentDevice()][texunit.unit] = filter;
	setMinMipFilters(texunit.unit);
}

void Graphics4::setTextureMipmapFilter(TextureUnit texunit, MipmapFilter filter) {
	mipFilters[System::currentDevice()][texunit.unit] = filter;
	setMinMipFilters(texunit.unit);
}

namespace {
	GLenum convert(Graphics4::BlendingOperation operation) {
		switch (operation) {
		case Graphics4::BlendZero:
			return GL_ZERO;
		case Graphics4::BlendOne:
			return GL_ONE;
		case Graphics4::SourceAlpha:
			return GL_SRC_ALPHA;
		case Graphics4::DestinationAlpha:
			return GL_DST_ALPHA;
		case Graphics4::InverseSourceAlpha:
			return GL_ONE_MINUS_SRC_ALPHA;
		case Graphics4::InverseDestinationAlpha:
			return GL_ONE_MINUS_DST_ALPHA;
		case Graphics4::SourceColor:
			return GL_SRC_COLOR;
		case Graphics4::DestinationColor:
			return GL_DST_COLOR;
		case Graphics4::InverseSourceColor:
			return GL_ONE_MINUS_SRC_COLOR;
		case Graphics4::InverseDestinationColor:
			return GL_ONE_MINUS_DST_COLOR;
		default:
			return GL_ONE;
		}
	}
}

void Graphics4::setTextureOperation(TextureOperation operation, TextureArgument arg1, TextureArgument arg2) {
	// glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void Graphics4::setBlendingMode(BlendingOperation source, BlendingOperation destination) {
	glBlendFunc(convert(source), convert(destination));
	glCheckErrors();
}

void Graphics4::setBlendingModeSeparate(BlendingOperation source, BlendingOperation destination, BlendingOperation alphaSource, BlendingOperation alphaDestination) {
	glBlendFuncSeparate(convert(source), convert(destination), convert(alphaSource), convert(alphaDestination));
	glCheckErrors();
}

void Graphics4::setRenderTarget(RenderTarget* texture, int num, int additionalTargets) {
	if (num == 0) {
		// TODO (DK) uneccessary?
		// System::makeCurrent(texture->contextId);
		glBindFramebuffer(GL_FRAMEBUFFER, texture->_framebuffer);
		glCheckErrors();
#if !defined(OPENGLES)
		if (texture->isCubeMap) glFramebufferTexture(GL_FRAMEBUFFER, texture->isDepthAttachment ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0, texture->_texture, 0); // Layered
#endif
		glViewport(0, 0, texture->width, texture->height);
		_renderTargetWidth = texture->width;
		_renderTargetHeight = texture->height;
		renderToBackbuffer = false;
		glCheckErrors();
	}

	if (additionalTargets > 0) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + num, GL_TEXTURE_2D, texture->_texture, 0);
		if (num == additionalTargets) {
			GLenum buffers[16];
			for (int i = 0; i <= additionalTargets; ++i) buffers[i] = GL_COLOR_ATTACHMENT0 + i;
#if defined(OPENGLES) && defined(SYS_ANDROID) && SYS_ANDROID_API >= 18
			((void (*)(GLsizei, GLenum*))glesDrawBuffers)(additionalTargets + 1, buffers);
#elif !defined(OPENGLES)
			glDrawBuffers(additionalTargets + 1, buffers);
#endif
		}
	}
}

void Graphics4::setRenderTargetFace(RenderTarget* texture, int face) {
	glBindFramebuffer(GL_FRAMEBUFFER, texture->_framebuffer);
	glCheckErrors();
	glFramebufferTexture2D(GL_FRAMEBUFFER, texture->isDepthAttachment ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, texture->_texture, 0);
	glViewport(0, 0, texture->width, texture->height);
	_renderTargetWidth = texture->width;
	_renderTargetHeight = texture->height;
	renderToBackbuffer = false;
	glCheckErrors();
}

void Graphics4::restoreRenderTarget() {
	glBindFramebuffer(GL_FRAMEBUFFER, originalFramebuffer[System::currentDevice()]);
	glCheckErrors();
	int w = System::windowWidth(System::currentDevice());
	int h = System::windowHeight(System::currentDevice());
	glViewport(0, 0, w, h);
	_renderTargetWidth = w;
	_renderTargetHeight = h;
	renderToBackbuffer = true;
	glCheckErrors();
}

bool Graphics4::renderTargetsInvertedY() {
	return true;
}

bool Graphics4::nonPow2TexturesSupported() {
	return true;
}

#if (defined(OPENGL) && !defined(SYS_PI) && !defined(SYS_ANDROID)) || (defined(SYS_ANDROID) && SYS_ANDROID_API >= 18)
bool Graphics4::initOcclusionQuery(uint* occlusionQuery) {
	glGenQueries(1, occlusionQuery);
	return true;
}

void Graphics4::deleteOcclusionQuery(uint occlusionQuery) {
	glDeleteQueries(1, &occlusionQuery);
}

#if defined(OPENGLES)
#define SAMPLES_PASSED GL_ANY_SAMPLES_PASSED
#else
#define SAMPLES_PASSED GL_SAMPLES_PASSED
#endif

void Graphics4::renderOcclusionQuery(uint occlusionQuery, int triangles) {
	glBeginQuery(SAMPLES_PASSED, occlusionQuery);
	glDrawArrays(GL_TRIANGLES, 0, triangles);
	glCheckErrors();
	glEndQuery(SAMPLES_PASSED);
}

bool Graphics4::isQueryResultsAvailable(uint occlusionQuery) {
	uint available;
	glGetQueryObjectuiv(occlusionQuery, GL_QUERY_RESULT_AVAILABLE, &available);
	return available != 0;
}

void Graphics4::getQueryResults(uint occlusionQuery, uint* pixelCount) {
	glGetQueryObjectuiv(occlusionQuery, GL_QUERY_RESULT, pixelCount);
}
#endif

void Graphics4::flush() {
	glFlush();
	glCheckErrors();
}