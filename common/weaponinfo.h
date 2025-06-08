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

#ifndef WEAPON_INFO_H
#define WEAPON_INFO_H

typedef struct weapon_data_s weapon_data_t;

struct weapon_data_s {
	int                        m_iId;                /*     0     4 */
	int                        m_iClip;              /*     4     4 */
	float                      m_flNextPrimaryAttack; /*     8     4 */
	float                      m_flNextSecondaryAttack; /*    12     4 */
	float                      m_flTimeWeaponIdle;   /*    16     4 */
	int                        m_fInReload;          /*    20     4 */
	int                        m_fInSpecialReload;   /*    24     4 */
	float                      m_flNextReload;       /*    28     4 */
	float                      m_flPumpTime;         /*    32     4 */
	float                      m_fReloadTime;        /*    36     4 */
	float                      m_fAimedDamage;       /*    40     4 */
	float                      m_fNextAimBonus;      /*    44     4 */
	int                        m_fInZoom;            /*    48     4 */
	int                        m_iWeaponState;       /*    52     4 */
	int                        iuser1;               /*    56     4 */
	int                        iuser2;               /*    60     4 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	int                        iuser3;               /*    64     4 */
	int                        iuser4;               /*    68     4 */
	float                      fuser1;               /*    72     4 */
	float                      fuser2;               /*    76     4 */
	float                      fuser3;               /*    80     4 */
	float                      fuser4;               /*    84     4 */

	/* size: 88, cachelines: 2, members: 22 */
	/* last cacheline: 24 bytes */
};

#endif
