file(GLOB LIBPUBLIC_FILES *.c)

execute_process(COMMAND "git describe --dirty --always" OUTPUT_VARIABLE BUILD_COMMIT)
execute_process(COMMAND "git rev-parse --abbrev-ref HEAD" OUTPUT_VARIABLE BUILD_BRANCH)
execute_process(COMMAND "git log -1 --format=%ci" OUTPUT_VARIABLE COMMIT_DATE)

add_library(public STATIC
	${LIBPUBLIC_FILES}
)

target_compile_definitions(public PUBLIC
	XASH_BUILD_COMMIT="\"${BUILD_COMMIT}\""
	XASH_BUILD_BRANCH="\"${BUILD_BRANCH}\""
	XASH_BUILD_COMMIT_DATE="\"${COMMIT_DATE}\""
)
