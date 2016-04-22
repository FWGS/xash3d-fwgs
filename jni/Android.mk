# override some setup.mk defines

override TARGET_arm_release_CFLAGS :=
override TARGET_thumb_release_CFLAGS :=
override TARGET_arm_debug_CFLAGS :=
override TARGET_thumb_debug_CFLAGS :=
override TARGET_CFLAGS :=

obj/local/x86/objs/xash/client/gl_studio.o: NDK_APP_CFLAGS += -a --aaaa -ftree-parallelize-loops=$(XASH_THREAD_NUM) aaa
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/cl_game.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/cl_frame.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/cl_parse.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_warp.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/cl_main.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/cl_tent.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/cl_events.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_sprite.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_image.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_cull.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_rpart.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_beams.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)

obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_rlight.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_rsurf.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_rmain.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)

obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/s_dsp.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/s_utils.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/s_vox.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/s_mouth.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/s_stream.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/s_load.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/s_mix.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_draw.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/client/gl_decals.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/soundlib/snd_wav.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/imagelib/img_tga.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/imagelib/img_wad.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/imagelib/img_bmp.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/imagelib/img_quant.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/console.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/net_buffer.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/net_encode.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/mod_studio.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/pm_trace.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/pm_surface.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/cvar.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/cmd.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/network.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/crtlib.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/crclib.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/filesystem.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/infostring.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/gamma.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/mathlib.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/matrixlib.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/common/touch.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/server/sv_move.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/server/sv_pmove.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/server/sv_world.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)
obj/local/$(TARGET_ARCH_ABI)/objs/xash/server/sv_phys.o: override NDK_APP_CFLAGS += -ftree-parallelize-loops=$(XASH_THREAD_NUM)

include $(call all-subdir-makefiles)
