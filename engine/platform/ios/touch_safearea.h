/*
 touch_safearea.h - iOS touch and menu viewport
 Copyright (C) 2026 Xash3D FWGS contributors
 SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

// Normalized insets; returns the window width in UIKit points (0 if unavailable).
float IOS_GetTouchInsets( void *window, float *left, float *top, float *right, float *bottom );
