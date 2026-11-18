/*
    The four GL entry points emscripten's WebGL2 library cannot take from the
    plugin as they are: glShaderSource, glEnable, glDisable and glIsEnabled.
    Every other GL call the plugin and the FFGL SDK make goes straight to
    emscripten's implementation, on the page's own WebGL2 context. (carousel's
    shim, which is nipkow's, with marbling's names.)

    glShaderSource
        The plugin hands GL desktop GLSL 4.10 (`#version 410 core`); WebGL2
        compiles GLSL ES 3.00. The page's `marblingShaderSource` first
        REQUIRES the text to be byte for byte one of the page's own
        assemblies of the plugin's shaders (demo/shaders.js, which
        demo/tools/check_shaders.py holds to source/Shaders.cpp, and its
        ASSEMBLY table to Assemble() and InitGL), then applies the demo kit's
        `port()`: the version line, the ES precision defaults and the removal
        of `precise`, nothing else -- and checks that it did exactly that
        (plugin.js, `shaderSource`). A shader the plugin assembled differently
        from the page's copy is refused (an empty source, so the plugin's
        InitGL fails and logs which pass), never compiled quietly.

    glEnable / glDisable / glIsEnabled
        GL_PROGRAM_POINT_SIZE is desktop-only state (in ES a point is always
        sized by the shader, and WebGL2 rejects the enum with INVALID_ENUM).
        The plugin's GLState.h saves and restores it on every frame, as a
        plugin inside Resolume must. Here it reads as off and enabling it is
        ignored; the plugin draws no points. Every other capability is passed
        through unchanged.

    Not shimmed: the plugin's allocations, PassBuffer's (FFGLFBO::Initialise)
    glTexImage2D( ..., GL_RGBA32F, ..., GL_RGBA, GL_FLOAT, NULL ) for the
    palette and the two map buffers, are already the pair OpenGL ES 3.0 takes
    for RGBA32F (ES 3.0.6, table 3.2), and their DEPTH_COMPONENT24
    renderbuffer is ES 3.0's too. The plugin reads nothing back. Rendering
    into an RGBA32F target needs EXT_color_buffer_float, which the page asks
    for and stops without; every float texture is read with texelFetch and
    nearest filtering, so no float filtering is needed.
*/
#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>

#include <cstring>
#include <string>

namespace
{
constexpr GLenum kProgramPointSize = 0x8642;///< GL_PROGRAM_POINT_SIZE, desktop GL 3.2

// clang-format off
EM_JS( void, maShaderSource, ( GLuint shader, const char* source ), {
	const object = GL.shaders[ shader ];
	const stage  = GLctx.getShaderParameter( object, 0x8B4F /* GL_SHADER_TYPE */ ) === 0x8B31 ? 'vertex' : 'fragment';
	GLctx.shaderSource( object, Module[ 'marblingShaderSource' ]( stage, UTF8ToString( source ) ) );
} );
EM_JS( void, maEnable, ( GLenum cap ), { GLctx.enable( cap ); } );
EM_JS( void, maDisable, ( GLenum cap ), { GLctx.disable( cap ); } );
EM_JS( int, maIsEnabled, ( GLenum cap ), { return GLctx.isEnabled( cap ) ? 1 : 0; } );
// clang-format on
} // namespace

extern "C"
{
void glShaderSource( GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length )
{
	std::string source;
	for( GLsizei i = 0; i < count; ++i )
	{
		if( string[ i ] == nullptr )
			continue;
		const size_t n = ( length != nullptr && length[ i ] >= 0 ) ? static_cast< size_t >( length[ i ] ) : std::strlen( string[ i ] );
		source.append( string[ i ], n );
	}
	maShaderSource( shader, source.c_str() );
}

void glEnable( GLenum cap )
{
	if( cap != kProgramPointSize )
		maEnable( cap );
}

void glDisable( GLenum cap )
{
	if( cap != kProgramPointSize )
		maDisable( cap );
}

GLboolean glIsEnabled( GLenum cap )
{
	if( cap == kProgramPointSize )
		return GL_FALSE;
	return maIsEnabled( cap ) ? GL_TRUE : GL_FALSE;
}
} // extern "C"
