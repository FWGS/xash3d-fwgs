/*
ref_device.h - common structures for retrieving GPU information for
refs, menu and engine
Copyright (C) 2021 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef REF_DEVICE_H
#define REF_DEVICE_H

// modeled after Vulkan and currently useful only for it
typedef enum ref_device_type_e
{
	REF_DEVICE_TYPE_OTHER = 0,
	REF_DEVICE_TYPE_INTERGRATED_GPU,
	REF_DEVICE_TYPE_DISCRETE_GPU,
	REF_DEVICE_TYPE_VIRTUAL_GPU,
	REF_DEVICE_TYPE_CPU,
	REF_DEVICE_TYPE_LAST,
} ref_device_type_t;

#define REF_DEVICE_NAME_SIZE 256

// only add new fields to the end of the struct!!!
typedef struct ref_device_s {
	int	vendorID;
	int	deviceID;
	ref_device_type_t	deviceType;
	char	deviceName[REF_DEVICE_NAME_SIZE];
} ref_device_t;

#endif /* REF_DEVICE_H */
