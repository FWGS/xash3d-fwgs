add_executable(xash3d #hl.exe
	game.cpp
)

target_compile_definitions(xash3d PRIVATE
	XASH_DISABLE_MENU_CHANGEGAME=0
)

target_link_libraries(xash3d
	dl
)

