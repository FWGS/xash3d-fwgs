file(GLOB LIBFILESYSTEM_FILES_C *.c)
file(GLOB LIBFILESYSTEM_FILES_CPP *.cpp)

add_library(filesystem_stdio SHARED
	${LIBFILESYSTEM_FILES_C}
	${LIBFILESYSTEM_FILES_CPP}
)

set_target_properties(filesystem_stdio PROPERTIES PREFIX "")

# TODO: this is very bad and should be fixed
target_compile_definitions(filesystem_stdio PRIVATE
	ALLOCA_H=<alloca.h>
)
