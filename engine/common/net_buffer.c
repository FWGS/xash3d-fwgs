/*
net_buffer.c - network bitbuffer io functions
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "protocol.h"
#include "net_buffer.h"
#include "xash3d_mathlib.h"

static const uint32_t BitWriteMasks[32][32] =
{ {
	0xfffffffe, 0xfffffffc, 0xfffffff8, 0xfffffff0, 0xffffffe0, 0xffffffc0, 0xffffff80, 0xffffff00,
	0xfffffe00, 0xfffffc00, 0xfffff800, 0xfffff000, 0xffffe000, 0xffffc000, 0xffff8000, 0xffff0000,
	0xfffe0000, 0xfffc0000, 0xfff80000, 0xfff00000, 0xffe00000, 0xffc00000, 0xff800000, 0xff000000,
	0xfe000000, 0xfc000000, 0xf8000000, 0xf0000000, 0xe0000000, 0xc0000000, 0x80000000, 0x00000000,
}, {
	0xfffffffd, 0xfffffff9, 0xfffffff1, 0xffffffe1, 0xffffffc1, 0xffffff81, 0xffffff01, 0xfffffe01,
	0xfffffc01, 0xfffff801, 0xfffff001, 0xffffe001, 0xffffc001, 0xffff8001, 0xffff0001, 0xfffe0001,
	0xfffc0001, 0xfff80001, 0xfff00001, 0xffe00001, 0xffc00001, 0xff800001, 0xff000001, 0xfe000001,
	0xfc000001, 0xf8000001, 0xf0000001, 0xe0000001, 0xc0000001, 0x80000001, 0x00000001, 0x00000001,
}, {
	0xfffffffb, 0xfffffff3, 0xffffffe3, 0xffffffc3, 0xffffff83, 0xffffff03, 0xfffffe03, 0xfffffc03,
	0xfffff803, 0xfffff003, 0xffffe003, 0xffffc003, 0xffff8003, 0xffff0003, 0xfffe0003, 0xfffc0003,
	0xfff80003, 0xfff00003, 0xffe00003, 0xffc00003, 0xff800003, 0xff000003, 0xfe000003, 0xfc000003,
	0xf8000003, 0xf0000003, 0xe0000003, 0xc0000003, 0x80000003, 0x00000003, 0x00000003, 0x00000003,
}, {
	0xfffffff7, 0xffffffe7, 0xffffffc7, 0xffffff87, 0xffffff07, 0xfffffe07, 0xfffffc07, 0xfffff807,
	0xfffff007, 0xffffe007, 0xffffc007, 0xffff8007, 0xffff0007, 0xfffe0007, 0xfffc0007, 0xfff80007,
	0xfff00007, 0xffe00007, 0xffc00007, 0xff800007, 0xff000007, 0xfe000007, 0xfc000007, 0xf8000007,
	0xf0000007, 0xe0000007, 0xc0000007, 0x80000007, 0x00000007, 0x00000007, 0x00000007, 0x00000007,
}, {
	0xffffffef, 0xffffffcf, 0xffffff8f, 0xffffff0f, 0xfffffe0f, 0xfffffc0f, 0xfffff80f, 0xfffff00f,
	0xffffe00f, 0xffffc00f, 0xffff800f, 0xffff000f, 0xfffe000f, 0xfffc000f, 0xfff8000f, 0xfff0000f,
	0xffe0000f, 0xffc0000f, 0xff80000f, 0xff00000f, 0xfe00000f, 0xfc00000f, 0xf800000f, 0xf000000f,
	0xe000000f, 0xc000000f, 0x8000000f, 0x0000000f, 0x0000000f, 0x0000000f, 0x0000000f, 0x0000000f,
}, {
	0xffffffdf, 0xffffff9f, 0xffffff1f, 0xfffffe1f, 0xfffffc1f, 0xfffff81f, 0xfffff01f, 0xffffe01f,
	0xffffc01f, 0xffff801f, 0xffff001f, 0xfffe001f, 0xfffc001f, 0xfff8001f, 0xfff0001f, 0xffe0001f,
	0xffc0001f, 0xff80001f, 0xff00001f, 0xfe00001f, 0xfc00001f, 0xf800001f, 0xf000001f, 0xe000001f,
	0xc000001f, 0x8000001f, 0x0000001f, 0x0000001f, 0x0000001f, 0x0000001f, 0x0000001f, 0x0000001f,
}, {
	0xffffffbf, 0xffffff3f, 0xfffffe3f, 0xfffffc3f, 0xfffff83f, 0xfffff03f, 0xffffe03f, 0xffffc03f,
	0xffff803f, 0xffff003f, 0xfffe003f, 0xfffc003f, 0xfff8003f, 0xfff0003f, 0xffe0003f, 0xffc0003f,
	0xff80003f, 0xff00003f, 0xfe00003f, 0xfc00003f, 0xf800003f, 0xf000003f, 0xe000003f, 0xc000003f,
	0x8000003f, 0x0000003f, 0x0000003f, 0x0000003f, 0x0000003f, 0x0000003f, 0x0000003f, 0x0000003f,
}, {
	0xffffff7f, 0xfffffe7f, 0xfffffc7f, 0xfffff87f, 0xfffff07f, 0xffffe07f, 0xffffc07f, 0xffff807f,
	0xffff007f, 0xfffe007f, 0xfffc007f, 0xfff8007f, 0xfff0007f, 0xffe0007f, 0xffc0007f, 0xff80007f,
	0xff00007f, 0xfe00007f, 0xfc00007f, 0xf800007f, 0xf000007f, 0xe000007f, 0xc000007f, 0x8000007f,
	0x0000007f, 0x0000007f, 0x0000007f, 0x0000007f, 0x0000007f, 0x0000007f, 0x0000007f, 0x0000007f,
}, {
	0xfffffeff, 0xfffffcff, 0xfffff8ff, 0xfffff0ff, 0xffffe0ff, 0xffffc0ff, 0xffff80ff, 0xffff00ff,
	0xfffe00ff, 0xfffc00ff, 0xfff800ff, 0xfff000ff, 0xffe000ff, 0xffc000ff, 0xff8000ff, 0xff0000ff,
	0xfe0000ff, 0xfc0000ff, 0xf80000ff, 0xf00000ff, 0xe00000ff, 0xc00000ff, 0x800000ff, 0x000000ff,
	0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff,
}, {
	0xfffffdff, 0xfffff9ff, 0xfffff1ff, 0xffffe1ff, 0xffffc1ff, 0xffff81ff, 0xffff01ff, 0xfffe01ff,
	0xfffc01ff, 0xfff801ff, 0xfff001ff, 0xffe001ff, 0xffc001ff, 0xff8001ff, 0xff0001ff, 0xfe0001ff,
	0xfc0001ff, 0xf80001ff, 0xf00001ff, 0xe00001ff, 0xc00001ff, 0x800001ff, 0x000001ff, 0x000001ff,
	0x000001ff, 0x000001ff, 0x000001ff, 0x000001ff, 0x000001ff, 0x000001ff, 0x000001ff, 0x000001ff,
}, {
	0xfffffbff, 0xfffff3ff, 0xffffe3ff, 0xffffc3ff, 0xffff83ff, 0xffff03ff, 0xfffe03ff, 0xfffc03ff,
	0xfff803ff, 0xfff003ff, 0xffe003ff, 0xffc003ff, 0xff8003ff, 0xff0003ff, 0xfe0003ff, 0xfc0003ff,
	0xf80003ff, 0xf00003ff, 0xe00003ff, 0xc00003ff, 0x800003ff, 0x000003ff, 0x000003ff, 0x000003ff,
	0x000003ff, 0x000003ff, 0x000003ff, 0x000003ff, 0x000003ff, 0x000003ff, 0x000003ff, 0x000003ff,
}, {
	0xfffff7ff, 0xffffe7ff, 0xffffc7ff, 0xffff87ff, 0xffff07ff, 0xfffe07ff, 0xfffc07ff, 0xfff807ff,
	0xfff007ff, 0xffe007ff, 0xffc007ff, 0xff8007ff, 0xff0007ff, 0xfe0007ff, 0xfc0007ff, 0xf80007ff,
	0xf00007ff, 0xe00007ff, 0xc00007ff, 0x800007ff, 0x000007ff, 0x000007ff, 0x000007ff, 0x000007ff,
	0x000007ff, 0x000007ff, 0x000007ff, 0x000007ff, 0x000007ff, 0x000007ff, 0x000007ff, 0x000007ff,
}, {
	0xffffefff, 0xffffcfff, 0xffff8fff, 0xffff0fff, 0xfffe0fff, 0xfffc0fff, 0xfff80fff, 0xfff00fff,
	0xffe00fff, 0xffc00fff, 0xff800fff, 0xff000fff, 0xfe000fff, 0xfc000fff, 0xf8000fff, 0xf0000fff,
	0xe0000fff, 0xc0000fff, 0x80000fff, 0x00000fff, 0x00000fff, 0x00000fff, 0x00000fff, 0x00000fff,
	0x00000fff, 0x00000fff, 0x00000fff, 0x00000fff, 0x00000fff, 0x00000fff, 0x00000fff, 0x00000fff,
}, {
	0xffffdfff, 0xffff9fff, 0xffff1fff, 0xfffe1fff, 0xfffc1fff, 0xfff81fff, 0xfff01fff, 0xffe01fff,
	0xffc01fff, 0xff801fff, 0xff001fff, 0xfe001fff, 0xfc001fff, 0xf8001fff, 0xf0001fff, 0xe0001fff,
	0xc0001fff, 0x80001fff, 0x00001fff, 0x00001fff, 0x00001fff, 0x00001fff, 0x00001fff, 0x00001fff,
	0x00001fff, 0x00001fff, 0x00001fff, 0x00001fff, 0x00001fff, 0x00001fff, 0x00001fff, 0x00001fff,
}, {
	0xffffbfff, 0xffff3fff, 0xfffe3fff, 0xfffc3fff, 0xfff83fff, 0xfff03fff, 0xffe03fff, 0xffc03fff,
	0xff803fff, 0xff003fff, 0xfe003fff, 0xfc003fff, 0xf8003fff, 0xf0003fff, 0xe0003fff, 0xc0003fff,
	0x80003fff, 0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff,
	0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff, 0x00003fff,
}, {
	0xffff7fff, 0xfffe7fff, 0xfffc7fff, 0xfff87fff, 0xfff07fff, 0xffe07fff, 0xffc07fff, 0xff807fff,
	0xff007fff, 0xfe007fff, 0xfc007fff, 0xf8007fff, 0xf0007fff, 0xe0007fff, 0xc0007fff, 0x80007fff,
	0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff,
	0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff, 0x00007fff,
}, {
	0xfffeffff, 0xfffcffff, 0xfff8ffff, 0xfff0ffff, 0xffe0ffff, 0xffc0ffff, 0xff80ffff, 0xff00ffff,
	0xfe00ffff, 0xfc00ffff, 0xf800ffff, 0xf000ffff, 0xe000ffff, 0xc000ffff, 0x8000ffff, 0x0000ffff,
	0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff,
	0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff,
}, {
	0xfffdffff, 0xfff9ffff, 0xfff1ffff, 0xffe1ffff, 0xffc1ffff, 0xff81ffff, 0xff01ffff, 0xfe01ffff,
	0xfc01ffff, 0xf801ffff, 0xf001ffff, 0xe001ffff, 0xc001ffff, 0x8001ffff, 0x0001ffff, 0x0001ffff,
	0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff,
	0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff,
}, {
	0xfffbffff, 0xfff3ffff, 0xffe3ffff, 0xffc3ffff, 0xff83ffff, 0xff03ffff, 0xfe03ffff, 0xfc03ffff,
	0xf803ffff, 0xf003ffff, 0xe003ffff, 0xc003ffff, 0x8003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff,
	0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff,
	0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff,
}, {
	0xfff7ffff, 0xffe7ffff, 0xffc7ffff, 0xff87ffff, 0xff07ffff, 0xfe07ffff, 0xfc07ffff, 0xf807ffff,
	0xf007ffff, 0xe007ffff, 0xc007ffff, 0x8007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff,
	0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff,
	0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff, 0x0007ffff,
}, {
	0xffefffff, 0xffcfffff, 0xff8fffff, 0xff0fffff, 0xfe0fffff, 0xfc0fffff, 0xf80fffff, 0xf00fffff,
	0xe00fffff, 0xc00fffff, 0x800fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff,
	0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff,
	0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff, 0x000fffff,
}, {
	0xffdfffff, 0xff9fffff, 0xff1fffff, 0xfe1fffff, 0xfc1fffff, 0xf81fffff, 0xf01fffff, 0xe01fffff,
	0xc01fffff, 0x801fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff,
	0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff,
	0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff,
}, {
	0xffbfffff, 0xff3fffff, 0xfe3fffff, 0xfc3fffff, 0xf83fffff, 0xf03fffff, 0xe03fffff, 0xc03fffff,
	0x803fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff,
	0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff,
	0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff, 0x003fffff,
}, {
	0xff7fffff, 0xfe7fffff, 0xfc7fffff, 0xf87fffff, 0xf07fffff, 0xe07fffff, 0xc07fffff, 0x807fffff,
	0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff,
	0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff,
	0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff, 0x007fffff,
}, {
	0xfeffffff, 0xfcffffff, 0xf8ffffff, 0xf0ffffff, 0xe0ffffff, 0xc0ffffff, 0x80ffffff, 0x00ffffff,
	0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff,
	0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff,
	0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff,
}, {
	0xfdffffff, 0xf9ffffff, 0xf1ffffff, 0xe1ffffff, 0xc1ffffff, 0x81ffffff, 0x01ffffff, 0x01ffffff,
	0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
	0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
	0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
}, {
	0xfbffffff, 0xf3ffffff, 0xe3ffffff, 0xc3ffffff, 0x83ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff,
	0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff,
	0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff,
	0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff,
}, {
	0xf7ffffff, 0xe7ffffff, 0xc7ffffff, 0x87ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff,
	0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff,
	0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff,
	0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff, 0x07ffffff,
}, {
	0xefffffff, 0xcfffffff, 0x8fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff,
	0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff,
	0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff,
	0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff,
}, {
	0xdfffffff, 0x9fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff,
	0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff,
	0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff,
	0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff,
}, {
	0xbfffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff,
	0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff,
	0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff,
	0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff,
}, {
	0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff,
	0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff,
	0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff,
	0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff,
} };

static const uint32_t ExtraMasks[32] =
{
	0x00000000, 0x00000001, 0x00000003, 0x00000007, 0x0000000f, 0x0000001f, 0x0000003f, 0x0000007f,
	0x000000ff, 0x000001ff, 0x000003ff, 0x000007ff, 0x00000fff, 0x00001fff, 0x00003fff, 0x00007fff,
	0x0000ffff, 0x0001ffff, 0x0003ffff, 0x0007ffff, 0x000fffff, 0x001fffff, 0x003fffff, 0x007fffff,
	0x00ffffff, 0x01ffffff, 0x03ffffff, 0x07ffffff, 0x0fffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff,
};

static const char *const clc_strings[clc_lastmsg+1] =
{
	"clc_bad",
	"clc_nop",
	"clc_move",
	"clc_stringcmd",
	"clc_delta",
	"clc_resourcelist",
	"clc_legacy_userinfo",
	"clc_fileconsistency",
	"clc_voicedata",
	"clc_cvarvalue/clc_goldsrc_hltv",
	"clc_cvarvalue2/clc_goldsrc_requestcvarvalue",
	"clc_goldsrc_requestcvarvalue2",
};

const char *const svc_strings[svc_lastmsg+1] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_event",
	"svc_changing",
	"svc_setview",
	"svc_sound",
	"svc_time",
	"svc_print",
	"svc_stufftext",
	"svc_setangle",
	"svc_serverdata",
	"svc_lightstyle",
	"svc_updateuserinfo",
	"svc_deltatable",
	"svc_clientdata",
	"svc_resource",
	"svc_pings",
	"svc_particle",
	"svc_restoresound",
	"svc_spawnstatic",
	"svc_event_reliable",
	"svc_spawnbaseline",
	"svc_temp_entity",
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_unused27",
	"svc_unused28",
	"svc_unused29",
	"svc_intermission",
	"svc_finale",
	"svc_cdtrack",
	"svc_restore",
	"svc_cutscene",
	"svc_weaponanim",
	"svc_bspdecal",
	"svc_roomtype",
	"svc_addangle",
	"svc_usermessage",
	"svc_packetentities",
	"svc_deltapacketentities",
	"svc_choke",
	"svc_resourcelist",
	"svc_deltamovevars",
	"svc_resourcerequest",
	"svc_customization",
	"svc_crosshairangle",
	"svc_soundfade",
	"svc_filetxferfailed",
	"svc_hltv",
	"svc_director",
	"svc_voiceinit",
	"svc_voicedata",
	"svc_unused54",
	"svc_unused55",
	"svc_resourcelocation",
	"svc_querycvarvalue",
	"svc_querycvarvalue2",
	"svc_exec",
};

const char *const svc_legacy_strings[svc_lastmsg+1] =
{
	[svc_legacy_changing] = "svc_legacy_changing",
	[svc_legacy_ambientsound] = "svc_legacy_ambientsound",
	[svc_legacy_soundindex] = "svc_legacy_soundindex",
	[svc_legacy_ambientsound] = "svc_legacy_ambientsound",
	[svc_legacy_modelindex] = "svc_legacy_modelindex",
	[svc_legacy_eventindex] = "svc_legacy_eventindex",
	[svc_legacy_chokecount] = "svc_legacy_chokecount",
};

const char *const svc_goldsrc_strings[svc_lastmsg+1] =
{
	[svc_goldsrc_version] = "svc_goldsrc_version",
	[svc_goldsrc_stopsound] = "svc_goldsrc_stopsound",
	[svc_goldsrc_damage] = "svc_goldsrc_damage",
	[svc_goldsrc_killedmonster] = "svc_goldsrc_killedmonster",
	[svc_goldsrc_foundsecret] = "svc_goldsrc_foundsecret",
	[svc_goldsrc_spawnstaticsound] = "svc_goldsrc_spawnstaticsound",
	[svc_goldsrc_decalname] = "svc_goldsrc_decalname",
	[svc_goldsrc_sendextrainfo] = "svc_goldsrc_sendextrainfo",
	[svc_goldsrc_timescale] = "svc_goldsrc_timescale",
};

const char *const svc_quake_strings[svc_lastmsg+1] =
{
	[svc_updatestat] = "svc_quake_updatestat",
	[svc_version] = "svc_quake_version",
	[svc_updatename] = "svc_quake_updatename",
	[svc_updatefrags] = "svc_quake_updatefrags",
	[svc_stopsound] = "svc_quake_stopsound",
	[svc_updatecolors] = "svc_quake_updatecolors",
	[svc_damage] = "svc_quake_damage",
	[svc_spawnbinary] = "svc_quake_spawnbinary",
	[svc_killedmonster] = "svc_quake_killedmonster",
	[svc_foundsecret] = "svc_quake_foundsecret",
	[svc_spawnstaticsound] = "svc_quake_spawnstaticsound",
	[svc_sellscreen] = "svc_quake_sellscreen",
	[svc_showlmp] = "svc_quake_showlmp",
	[svc_hidelmp] = "svc_quake_hidelmp",
	[svc_skybox] = "svc_quake_skybox",
	[svc_skyboxsize] = "svc_quake_skyboxsize",
	[svc_fog] = "svc_quake_fog",
};

void MSG_WriteUBitLong( sizebuf_t *sb, uint curData, int numbits )
{
	int	nBitsLeft = numbits;
	int	iCurBit = sb->iCurBit;
	uint	iDWord = iCurBit >> 5;	// Mask in a dword.
	uint32_t	iCurBitMasked;
	int	nBitsWritten;

	Assert( numbits >= 1 && numbits <= 32 );

	// bounds checking..
	if( MSG_Overflow( sb, numbits ))
	{
		sb->iCurBit = sb->nDataBits;
		return;
	}

	iCurBitMasked = iCurBit & 31;
	((uint32_t *)sb->pData)[iDWord] &= BitWriteMasks[iCurBitMasked][nBitsLeft-1];
	((uint32_t *)sb->pData)[iDWord] |= curData << iCurBitMasked;

	// did it span a dword?
	nBitsWritten = 32 - iCurBitMasked;

	if( nBitsWritten < nBitsLeft )
	{
		nBitsLeft -= nBitsWritten;
		iCurBit += nBitsWritten;
		curData >>= nBitsWritten;

		iCurBitMasked = iCurBit & 31;
		((uint32_t *)sb->pData)[iDWord+1] &= BitWriteMasks[iCurBitMasked][nBitsLeft-1];
		((uint32_t *)sb->pData)[iDWord+1] |= curData << iCurBitMasked;
	}
	sb->iCurBit += numbits;
}

/*
=======================
MSG_WriteSBitLong

sign bit comes first
=======================
*/
void MSG_WriteSBitLong( sizebuf_t *sb, int data, int numbits )
{
	// do we have a valid # of bits to encode with?
	Assert( numbits >= 1 && numbits <= 32 );

	if( sb->iAlternateSign )
	{
		MSG_WriteOneBit( sb, data < 0 ? 1 : 0 );
		MSG_WriteUBitLong( sb, (uint)abs( data ), numbits - 1 );
	}
	else
	{
		if( data < 0 )
		{
			MSG_WriteUBitLong( sb, (uint)( 0x80000000 + data ), numbits - 1 );
			MSG_WriteOneBit( sb, 1 );
		}
		else
		{
			MSG_WriteUBitLong( sb, (uint)data, numbits - 1 );
			MSG_WriteOneBit( sb, 0 );
		}
	}
}

void MSG_WriteBitLong( sizebuf_t *sb, uint data, int numbits, qboolean bSigned )
{
	if( bSigned )
		MSG_WriteSBitLong( sb, (int)data, numbits );
	else MSG_WriteUBitLong( sb, data, numbits );
}

qboolean MSG_WriteBits( sizebuf_t *sb, const void *pData, int nBits )
{
	byte	*pOut = (byte *)pData;
	int	nBitsLeft = nBits;

	// get output dword-aligned.
	while((( uintptr_t )pOut & 3 ) != 0 && nBitsLeft >= 8 )
	{
		MSG_WriteUBitLong( sb, *pOut, 8 );

		nBitsLeft -= 8;
		++pOut;
	}

	// read dwords.
	while( nBitsLeft >= 32 )
	{
		MSG_WriteUBitLong( sb, *(( uint32_t *)pOut ), 32 );

		pOut += sizeof( uint32_t );
		nBitsLeft -= 32;
	}

	// read the remaining bytes.
	while( nBitsLeft >= 8 )
	{
		MSG_WriteUBitLong( sb, *pOut, 8 );

		nBitsLeft -= 8;
		++pOut;
	}

	// Read the remaining bits.
	if( nBitsLeft )
	{
		MSG_WriteUBitLong( sb, *pOut, nBitsLeft );
	}

	return !sb->bOverflow;
}

void MSG_WriteBitAngle( sizebuf_t *sb, float fAngle, int numbits )
{
	const uint shift = ( 1 << numbits );
	const uint mask = shift - 1;
	int	d;

	// clamp the angle before receiving
	fAngle = fmod( fAngle, 360.0f );
	if( fAngle < 0 ) fAngle += 360.0f;

	d = (int)(( fAngle * shift ) / 360.0f );
	d &= mask;

	MSG_WriteUBitLong( sb, (uint)d, numbits );
}

void MSG_WriteCoord( sizebuf_t *sb, float val )
{
	// g-cont. we loose precision here but keep old size of coord variable!
	if( FBitSet( host.features, ENGINE_WRITE_LARGE_COORD ))
		MSG_WriteShort( sb, Q_rint( val ));
	else MSG_WriteShort( sb, (int)( val * 8.0f ));
}

void MSG_WriteVec3Coord( sizebuf_t *sb, const float *fa )
{
	MSG_WriteCoord( sb, fa[0] );
	MSG_WriteCoord( sb, fa[1] );
	MSG_WriteCoord( sb, fa[2] );
}

void MSG_WriteVec3Angles( sizebuf_t *sb, const float *fa )
{
	MSG_WriteBitAngle( sb, fa[0], 16 );
	MSG_WriteBitAngle( sb, fa[1], 16 );
	MSG_WriteBitAngle( sb, fa[2], 16 );
}

void MSG_WriteCmdExt( sizebuf_t *sb, int cmd, netsrc_t type, const char *name )
{
	if( unlikely( net_send_debug.value ))
	{
		if( name != NULL )
		{
			// get custom name
			Con_Printf( "^1sv^7 (%d) write: %s\n", sb->iCurBit, name );
		}
		else if( type == NS_SERVER )
		{
			if( cmd >= 0 && cmd <= svc_lastmsg )
			{
				// get engine message name
				Con_Printf( "^1sv^7 (%d) write: %s\n", sb->iCurBit, svc_strings[cmd] );
			}
		}
		else if( type == NS_CLIENT )
		{
			if( cmd >= 0 && cmd <= clc_lastmsg && cmd != clc_nop )
			{
				Con_Printf( "^1cl^7 (%d) write: %s\n", sb->iCurBit, clc_strings[cmd] );
			}
		}
	}

	MSG_WriteUBitLong( sb, cmd, sizeof( uint8_t ) << 3 );
}

void MSG_WriteChar( sizebuf_t *sb, int val )
{
	MSG_WriteSBitLong( sb, val, sizeof( int8_t ) << 3 );
}

void MSG_WriteByte( sizebuf_t *sb, int val )
{
	MSG_WriteUBitLong( sb, val, sizeof( uint8_t ) << 3 );
}

void MSG_WriteShort( sizebuf_t *sb, int val )
{
	MSG_WriteSBitLong( sb, val, sizeof( int16_t ) << 3 );
}

void MSG_WriteWord( sizebuf_t *sb, int val )
{
	MSG_WriteUBitLong( sb, val, sizeof( uint16_t ) << 3 );
}

void MSG_WriteLong( sizebuf_t *sb, int val )
{
	MSG_WriteSBitLong( sb, val, sizeof( int32_t ) << 3 );
}

void MSG_WriteDword( sizebuf_t *sb, uint val )
{
	MSG_WriteUBitLong( sb, val, sizeof( uint32_t ) << 3 );
}

void MSG_WriteFloat( sizebuf_t *sb, float val )
{
	MSG_WriteBits( sb, &val, sizeof( val ) << 3 );
}

qboolean MSG_WriteBytes( sizebuf_t *sb, const void *pBuf, int nBytes )
{
	return MSG_WriteBits( sb, pBuf, nBytes << 3 );
}

qboolean MSG_WriteString( sizebuf_t *sb, const char *pStr )
{
	int len = Q_strlen( pStr );

	if( len )
		MSG_WriteBytes( sb, pStr, len + 1 );
	else MSG_WriteByte( sb, 0 );

	return !sb->bOverflow;
}

qboolean MSG_WriteStringf( sizebuf_t *sb, const char *format, ... )
{
	va_list va;
	int len;
	char buf[MAX_VA_STRING];

	va_start( va, format );
	len = Q_vsnprintf( buf, sizeof( buf ), format, va );
	va_end( va );

	if( len < 0 )
	{
		Host_Error( "%s: snprintf overflow!\n", __func__ );
		return false;
	}

	MSG_WriteBytes( sb, buf, len + 1 );

	return !sb->bOverflow;
}

int MSG_ReadOneBit( sizebuf_t *sb )
{
	if( !MSG_Overflow( sb, 1 ))
	{
		int value = sb->pData[sb->iCurBit >> 3] & (1 << ( sb->iCurBit & 7 ));
		sb->iCurBit++;
		return !!value;
	}
	return 0;
}

uint MSG_ReadUBitLong( sizebuf_t *sb, int numbits )
{
	int	idword1;
	uint	dword1, ret;

	if( numbits == 8 )
	{
		int leftBits = MSG_GetNumBitsLeft( sb );

		if( leftBits >= 0 && leftBits < 8 )
			return 0;	// end of message
	}

	if( MSG_Overflow( sb, numbits ))
	{
		sb->iCurBit = sb->nDataBits;
		return 0;
	}

	Assert( numbits > 0 && numbits <= 32 );

	// Read the current dword.
	idword1 = sb->iCurBit >> 5;
	dword1 = ((uint *)sb->pData)[idword1];
	dword1 >>= ( sb->iCurBit & 31 );	// get the bits we're interested in.

	sb->iCurBit += numbits;
	ret = dword1;

	// Does it span this dword?
	if(( sb->iCurBit - 1 ) >> 5 == idword1 )
	{
		if( numbits != 32 )
			ret &= ExtraMasks[numbits];
	}
	else
	{
		int	nExtraBits = sb->iCurBit & 31;
		uint	dword2 = ((uint *)sb->pData)[idword1+1] & ExtraMasks[nExtraBits];

		// no need to mask since we hit the end of the dword.
		// shift the second dword's part into the high bits.
		ret |= (dword2 << ( numbits - nExtraBits ));
	}
	return ret;
}

qboolean MSG_ReadBits( sizebuf_t *sb, void *pOutData, int nBits )
{
	byte	*pOut = (byte *)pOutData;
	int	nBitsLeft = nBits;

	// get output dword-aligned.
	while((( uintptr_t )pOut & 3) != 0 && nBitsLeft >= 8 )
	{
		*pOut = (byte)MSG_ReadUBitLong( sb, 8 );
		++pOut;
		nBitsLeft -= 8;
	}

	// read dwords.
	while( nBitsLeft >= 32 )
	{
		*((uint32_t *)pOut) = MSG_ReadUBitLong( sb, 32 );
		pOut += sizeof( uint32_t );
		nBitsLeft -= 32;
	}

	// read the remaining bytes.
	while( nBitsLeft >= 8 )
	{
		*pOut = MSG_ReadUBitLong( sb, 8 );
		++pOut;
		nBitsLeft -= 8;
	}

	// read the remaining bits.
	if( nBitsLeft )
	{
		*pOut = MSG_ReadUBitLong( sb, nBitsLeft );
	}

	return !sb->bOverflow;
}

float MSG_ReadBitAngle( sizebuf_t *sb, int numbits )
{
	float shift = (float)( 1 << numbits );
	int i = MSG_ReadUBitLong( sb, numbits );
	float fReturn = (float)i * ( 360.0f / shift );

	// clamp the finale angle
	if( fReturn < -180.0f ) fReturn += 360.0f;
	else if( fReturn > 180.0f ) fReturn -= 360.0f;

	return fReturn;
}

// Append numbits least significant bits from data to the current bit stream
int MSG_ReadSBitLong( sizebuf_t *sb, int numbits )
{
	int r;

	if( sb->iAlternateSign )
	{
		int sign = MSG_ReadOneBit( sb );
		r = MSG_ReadUBitLong( sb, numbits - 1 );

		if( sign )
			r = -r;
	}
	else
	{
		r = MSG_ReadUBitLong( sb, numbits - 1 );
		if( MSG_ReadOneBit( sb ))
			r = -( BIT( numbits - 1 ) - r );
	}

	return r;
}

uint MSG_ReadBitLong( sizebuf_t *sb, int numbits, qboolean bSigned )
{
	if( bSigned )
		return (uint)MSG_ReadSBitLong( sb, numbits );
	return MSG_ReadUBitLong( sb, numbits );
}

int MSG_ReadCmd( sizebuf_t *sb, netsrc_t type )
{
	int	cmd = MSG_ReadUBitLong( sb, sizeof( uint8_t ) << 3 );

	if( unlikely( net_recv_debug.value ))
	{
		if( type == NS_SERVER )
		{
			if( cmd != svc_nop )
				Con_Printf( "^1cl^7 read: %s\n", CL_MsgInfo( cmd ));
		}
		else if( cmd >= 0 && cmd <= clc_lastmsg )
		{
			Con_Printf( "^1sv^7 read: %s\n", clc_strings[cmd] );
		}
	}

	return cmd;
}

int MSG_ReadChar( sizebuf_t *sb )
{
	int alt = sb->iAlternateSign, ret;

	sb->iAlternateSign = 0;
	ret = MSG_ReadSBitLong( sb, sizeof( int8_t ) << 3 );
	sb->iAlternateSign = alt;

	return ret;
}

int MSG_ReadByte( sizebuf_t *sb )
{
	return MSG_ReadUBitLong( sb, sizeof( uint8_t ) << 3 );
}

int MSG_ReadShort( sizebuf_t *sb )
{
	int alt = sb->iAlternateSign, ret;

	sb->iAlternateSign = 0;
	ret = MSG_ReadSBitLong( sb, sizeof( int16_t ) << 3 );
	sb->iAlternateSign = alt;

	return ret;
}

int MSG_ReadWord( sizebuf_t *sb )
{
	return MSG_ReadUBitLong( sb, sizeof( uint16_t ) << 3 );
}

float MSG_ReadCoord( sizebuf_t *sb )
{
	// g-cont. we loose precision here but keep old size of coord variable!
	if( FBitSet( host.features, ENGINE_WRITE_LARGE_COORD ))
		return (float)(MSG_ReadShort( sb ));
	return (float)(MSG_ReadShort( sb ) * ( 1.0f / 8.0f ));
}

void MSG_ReadVec3Coord( sizebuf_t *sb, vec3_t fa )
{
	fa[0] = MSG_ReadCoord( sb );
	fa[1] = MSG_ReadCoord( sb );
	fa[2] = MSG_ReadCoord( sb );
}

void MSG_ReadVec3Angles( sizebuf_t *sb, vec3_t fa )
{
	fa[0] = MSG_ReadBitAngle( sb, 16 );
	fa[1] = MSG_ReadBitAngle( sb, 16 );
	fa[2] = MSG_ReadBitAngle( sb, 16 );
}

int MSG_ReadLong( sizebuf_t *sb )
{
	int alt = sb->iAlternateSign, ret;

	sb->iAlternateSign = 0;
	ret = MSG_ReadSBitLong( sb, sizeof( int32_t ) << 3 );
	sb->iAlternateSign = alt;

	return ret;
}

uint MSG_ReadDword( sizebuf_t *sb )
{
	return MSG_ReadUBitLong( sb, sizeof( uint32_t ) << 3 );
}

float MSG_ReadFloat( sizebuf_t *sb )
{
	float	ret;

	MSG_ReadBits( sb, &ret, sizeof( ret ) << 3 );

	return ret;
}

qboolean MSG_ReadBytes( sizebuf_t *sb, void *pOut, int nBytes )
{
	return MSG_ReadBits( sb, pOut, nBytes << 3 );
}

static char *MSG_ReadStringExt( sizebuf_t *sb, qboolean bLine )
{
	static char	string[4096];
	int		l = 0;

	do
	{
		// use MSG_ReadByte so -1 is out of bounds
		int c = MSG_ReadByte( sb );

		if( c == 0 ) break;
		else if( bLine && c == '\n' )
			break;

		// translate all fmt spec to avoid crash bugs
		// NOTE: but game strings leave unchanged. see pfnWriteString for details
		if( c == '%' ) c = '.';

		string[l] = c;
		l++;
	} while( l < sizeof( string ) - 1 );
	string[l] = 0; // terminator

	return string;
}

char *MSG_ReadString( sizebuf_t *sb )
{
	return MSG_ReadStringExt( sb, false );
}

char *MSG_ReadStringLine( sizebuf_t *sb )
{
	return MSG_ReadStringExt( sb, true );
}

void MSG_ExciseBits( sizebuf_t *sb, int startbit, int bitstoremove )
{
	int	i, endbit = startbit + bitstoremove;
	int	remaining_to_end = sb->nDataBits - endbit;
	sizebuf_t	temp;

	MSG_StartWriting( &temp, sb->pData, MSG_GetMaxBytes( sb ), startbit, -1 );
	MSG_SeekToBit( sb, endbit, SEEK_SET );

	for( i = 0; i < remaining_to_end; i++ )
	{
		MSG_WriteOneBit( &temp, MSG_ReadOneBit( sb ));
	}

	MSG_SeekToBit( sb, startbit, SEEK_SET );
	sb->nDataBits -= bitstoremove;
}

#ifdef XASH_ENGINE_TESTS
#include "tests.h"

static const void *g_testbuf = "asdf\xba\xa1\xba\xa1\xed\xc8\x15\x7a";
static const size_t g_testbuf_bits = (( 4 + 4 + 2 + 1 ) << 3 ) + 4;

static void Test_Buffer_BitByte( void )
{
	TASSERT_EQi( BitByte( 0 ), 0 );
	TASSERT_EQi( BitByte( 1 ), 1 );
	TASSERT_EQi( BitByte( 8 ), 1 );
	TASSERT_EQi( BitByte( 9 ), 2 );
}

static void Test_Buffer_Write( void )
{
	sizebuf_t sb;
	char testdata[0x100] = { 0 };

	MSG_Init( &sb, __func__, testdata, sizeof( testdata ));
	TASSERT_EQi( sb.iCurBit, 0 );
	TASSERT_EQi( sb.nDataBits, sizeof( testdata ) << 3 );
	TASSERT_EQp( sb.pData, (void *)testdata );
	TASSERT_EQi( sb.bOverflow, false );

	MSG_WriteBytes( &sb, "asdf", 4 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 32 );

	MSG_WriteDword( &sb, 0xa1baa1ba );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 64 );

	MSG_WriteShort( &sb, -0x3713 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 80 );

	MSG_WriteOneBit( &sb, 1 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 81 );

	MSG_WriteOneBit( &sb, 0 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 82 );

	MSG_WriteOneBit( &sb, 1 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 83 );

	MSG_WriteOneBit( &sb, 0 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 84 );

	MSG_WriteByte( &sb, 0xa1 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 92 );

	TASSERT_EQi( MSG_GetNumBitsWritten( &sb ), g_testbuf_bits );
	TASSERT_EQi( MSG_GetNumBytesWritten( &sb ), BitByte( g_testbuf_bits ));
	TASSERT_EQi( MSG_GetRealBytesWritten( &sb ), g_testbuf_bits >> 3 );

	// if tests fails here on big endian, it's possible due to endian issues
	TASSERT( !memcmp( sb.pData, g_testbuf, g_testbuf_bits >> 3 ));

	// must check last 4 bits separately because we never care about uninitialized bits
	MSG_SeekToBit( &sb, g_testbuf_bits & ~7, SEEK_SET );
	TASSERT_EQi( sb.iCurBit, 88 );
	TASSERT_EQi( MSG_ReadUBitLong( &sb, 4 ), 0xa );
}

static void Test_Buffer_Read( void )
{
	sizebuf_t sb;
	char buf[4];

	MSG_StartReading( &sb, (void *)g_testbuf, -1, 0, g_testbuf_bits );
	TASSERT_EQi( sb.iCurBit, 0 );
	TASSERT_EQi( sb.nDataBits, g_testbuf_bits );
	TASSERT_EQp( sb.pData, (void *)g_testbuf );
	TASSERT_EQi( sb.bOverflow, false );

	MSG_ReadBytes( &sb, buf, 4 );
	TASSERT( !memcmp( buf, "asdf", 4 ));
	TASSERT_EQi( sb.iCurBit, 32 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadWord( &sb ), 0xa1ba );
	TASSERT_EQi( sb.iCurBit, 48 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadDword( &sb ), 0xc8eda1baU );
	TASSERT_EQi( sb.iCurBit, 80 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadOneBit( &sb ), 1 );
	TASSERT_EQi( sb.iCurBit, 81 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadOneBit( &sb ), 0 );
	TASSERT_EQi( sb.iCurBit, 82 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadOneBit( &sb ), 1 );
	TASSERT_EQi( sb.iCurBit, 83 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadOneBit( &sb ), 0 );
	TASSERT_EQi( sb.iCurBit, 84 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadByte( &sb ), 0xa1 );
	TASSERT_EQi( sb.iCurBit, 92 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_Overflow( &sb, 1 ), true );
	TASSERT_EQi( sb.bOverflow, true );
}

static void Test_Buffer_ExciseBits( void )
{
	sizebuf_t sb;
	char testdata[0x100];

	memcpy( testdata, g_testbuf, BitByte( g_testbuf_bits ));

	MSG_StartWriting( &sb, testdata, 0, 0, g_testbuf_bits );
	MSG_ExciseBits( &sb, 8, 28 );

	TASSERT_EQi( MSG_CheckOverflow( &sb ), false );
	TASSERT_EQi( MSG_GetMaxBits( &sb ), 64 );
	TASSERT( !memcmp( MSG_GetData( &sb ), "a\x1b\xaa\x1b\xda\x8e\x5c\xa1", 8 ));

	memcpy( testdata, g_testbuf, BitByte( g_testbuf_bits ));

	MSG_StartWriting( &sb, testdata, 0, 0, g_testbuf_bits );
	MSG_ExciseBits( &sb, 16, 32 );

	TASSERT_EQi( MSG_CheckOverflow( &sb ), false );
	TASSERT_EQi( MSG_GetMaxBits( &sb ), g_testbuf_bits - 32 );
	TASSERT( !memcmp( MSG_GetData( &sb ), "as\xba\xa1\xed\xc8\x15", 7 ));

	MSG_SeekToBit( &sb, 7 << 3, SEEK_SET );
	TASSERT_EQi( MSG_ReadUBitLong( &sb, 4 ), 0xa );
}

void Test_RunBuffer( void )
{
	TRUN( Test_Buffer_BitByte( ));
	TRUN( Test_Buffer_Write( ));
	TRUN( Test_Buffer_Read( ));
	TRUN( Test_Buffer_ExciseBits( ));
}

#endif // XASH_ENGINE_TESTS
