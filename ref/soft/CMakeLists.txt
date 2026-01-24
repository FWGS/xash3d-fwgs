file(GLOB REF_SOFT_FILES *.c)

add_library(ref_soft SHARED
	${REF_SOFT_FILES}
)

target_compile_definitions(ref_soft PRIVATE
	REF_DLL=1
)

target_link_libraries(ref_soft
	m
	public
)
