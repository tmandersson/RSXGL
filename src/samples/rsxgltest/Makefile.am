CC = @PPU_CC@
CXX = @PPU_CXX@
AR = @PPU_AR@
RANLIB = @PPU_RANLIB@
LD = @PPU_LD@

NV40ASM = $(top_builddir)/src/cgcomp/nv40asm

eigen_LOCATION = $(top_srcdir)/extsrc/eigen
eigen_CPPFLAGS = -I$(eigen_LOCATION)

include $(srcdir)/../samples.am

noinst_PROGRAMS = rsxgltest.elf

BUILT_SOURCES = textures_vpo.h textures_fpo.h \
	dice_bin.h nagel_bin.h \
	cube_vpo.h cube_fpo.h \
	manypoints_vpo.h manypoints_fpo.h \
	texcube_vpo.h texcube_fpo.h
CLEANFILES = draw_vpo.h draw_fpo.h draw_vpo.o draw_fpo.o \
	textures_vpo.h textures_fpo.h textures_vpo.o textures_fpo.o \
	cube_vpo.h cube_fpo.h cube_vpo.o cube_fpo.o \
	manypoints_vpo.h manypoints_fpo.h manypoints_vpo.o manypoints_fpo.o \
	texcube_vpo.h texcube_fpo.h texcube_vpo.o texcube_fpo.o \
	dice_bin.o dice_bin.h \
	bagel_bin.o nagel_bin.h

# clear.c viewport_scissor.c buffer.c program.c draw.c uniforms.c textures.c cube.cc
cube_objects = cube_vpo.o cube_fpo.o
cube_sources = cube.cc cube.vcg cube.fcg

textures_objects = textures_vpo.o textures_fpo.o dice_bin.o
textures_sources = textures.c texture.c textures.vert textures.frag dice.bin

manypoints_objects = manypoints_vpo.o manypoints_fpo.o
manypoints_sources = manypoints.cc manypoints.vcg manypoints.fcg

manycubes_objects = cube_vpo.o cube_fpo.o
manycubes_sources = manycubes.cc cube.vcg cube.fcg

manycubestream_objects = cube_vpo.o cube_fpo.o
manycubestream_sources = manycubestream.cc cube.vcg cube.fcg

texcube_objects = texcube_vpo.o texcube_fpo.o nagel_bin.o
texcube_sources = texcube.cc texture.c texcube.vcg texcube.fcg nagel.bin

objects = $(texcube_objects)
sources = $(texcube_sources)

rsxgltest_elf_DEPENDENCIES = $(top_builddir)/src/library/libEGL.a $(top_builddir)/src/library/libGL.a	\
	$(objects)
rsxgltest_elf_SOURCES = main.c			\
	math3d.cc sine_wave.c			\
	$(sources)

rsxgltest_elf_CPPFLAGS = -D__RSX__ -I\$(top_srcdir)/include $(eigen_CPPFLAGS) $(PS3DEV_PPU_CPPFLAGS) $(PSL1GHT_PPU_CPPFLAGS) $(PORTLIBS_PPU_CPPFLAGS) -I$(top_srcdir)/include -I$(top_srcdir)/src/library -I$(top_builddir)/src/library
rsxgltest_elf_CFLAGS = -std=gnu99 -fgnu89-inline -O3
rsxgltest_elf_LDFLAGS = $(PS3DEV_PPU_LDFLAGS) $(PSL1GHT_PPU_LDFLAGS) $(PORTLIBS_PPU_LDFLAGS) -L$(top_builddir)/src/library
rsxgltest_elf_LDADD = $(objects) -lEGL -lGL -lrsx -lgcm_sys -lnet -lio -lsysutil -lsysmodule -lpng -lz -lstdc++ -lm -lrt -llv2

TITLE           :=      RSXGL Test
APPID           :=      TSTRSXGL0
CONTENTID       :=      UP0001-$(APPID)_00-0000000000000000
PKGFILES        :=      release
override ICON0		:=	$(srcdir)/../brewcrab.png

pkg: rsxgltest.pkg

all-local: pkg

clean-local:
	rm -rf pkg rsxgltest.self rsxgltest.pkg 
