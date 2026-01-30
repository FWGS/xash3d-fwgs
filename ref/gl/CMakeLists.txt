include_directories(
	./
)

file(GLOB REF_GL_FILES
	*.c
	gl2_shim/*.c
)

# Find OpenGL
find_package(OpenGL REQUIRED)

add_library(ref_gl SHARED
	${REF_GL_FILES}
)

target_compile_definitions(ref_gl PRIVATE
	REF_DLL=1
)

target_link_libraries(ref_gl
	${OPENGL_LIBRARIES}
	m
	public
)
