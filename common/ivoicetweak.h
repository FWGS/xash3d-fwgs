/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org>
*/

#ifndef VOICETWEAK_H
#define VOICETWEAK_H

typedef struct IVoiceTweak_s IVoiceTweak;

typedef enum {
	MicrophoneVolume  = 0,
	OtherSpeakerScale = 1,
	MicBoost          = 2,
} VoiceTweakControl;

struct IVoiceTweak_s {
	int                        (*StartVoiceTweakMode)(void); /*     0     4 */
	void                       (*EndVoiceTweakMode)(void); /*     4     4 */
	void                       (*SetControlFloat)(VoiceTweakControl, float); /*     8     4 */
	float                      (*GetControlFloat)(VoiceTweakControl); /*    12     4 */
	int                        (*GetSpeakingVolume)(void); /*    16     4 */

	/* size: 20, cachelines: 1, members: 5 */
	/* last cacheline: 20 bytes */
};

#endif
