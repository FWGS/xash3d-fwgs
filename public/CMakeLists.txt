file(GLOB LIBPUBLIC_FILES *.c)

execute_process(COMMAND "git show | head -n 1 | cut -f2 -d' ' | head -c 8" OUTPUT_VARIABLE BUILD_COMMIT)
execute_process(COMMAND "git status | head -n 1 | sed 's/On branch //'" OUTPUT_VARIABLE BUILD_BRANCH)
execute_process(COMMAND "TODO" OUTPUT_VARIABLE COMMIT_DATE)

add_library(public STATIC
	${LIBPUBLIC_FILES}
)

target_compile_definitions(public PUBLIC
	XASH_BUILD_COMMIT="\"${BUILD_COMMIT}\""
	XASH_BUILD_BRANCH="\"${BUILD_BRANCH}\""
	XASH_BUILD_COMMIT_DATE="\"${COMMIT_DATE}\""
)
