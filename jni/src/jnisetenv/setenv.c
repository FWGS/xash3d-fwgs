/*
setenv.c -- a jni wrapper for setenv() call for XashActivity
Copyright (C) 2016 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <jni.h>
#include <stdlib.h>

JNIEXPORT int JNICALL Java_in_celest_xash3d_XashActivity_setenv( JNIEnv* env, jclass clazz, jstring key, jstring value, jboolean overwrite )
{
	char* k = (char *) (*env)->GetStringUTFChars(env, key, NULL);
	char* v = (char *) (*env)->GetStringUTFChars(env, value, NULL);
	int err = setenv(k, v, overwrite);
	(*env)->ReleaseStringUTFChars(env, key, k);
	(*env)->ReleaseStringUTFChars(env, value, v);
	return err;
}
