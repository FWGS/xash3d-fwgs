/*
ps4_sync.c - working semaphores for PS4
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*
Static musl libc from OpenOrbis toolchain implements unnamed semaphores
through system ksem_*() functions, but passes sem_t pointer where kernel
expects semaphore id, so every wait fails immediately. SDL timer thread
waits on a semaphore forever and turned into a busy loop, reporting
the error on every iteration.

The linker redirects sem_* functions here with --wrap=<function>
(see scripts/waifulib/ps4.py). Semaphore is a plain atomic counter stored
in sem_t, waiting is done by polling with a short sleep, which is fine
for SDL timer and thread startup, the only users so far.
*/

#include <errno.h>
#include <semaphore.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define PS4_SEM_SPIN      64
#define PS4_SEM_POLL_USEC 500

int32_t sceKernelUsleep( uint32_t usec );

static volatile int *PS4_SemCounter( sem_t *sem )
{
	return &sem->__val[0];
}

int __wrap_sem_init( sem_t *sem, int pshared, unsigned value )
{
	if( value > 0x7fffffff )
	{
		errno = EINVAL;
		return -1;
	}

	// process-shared semaphores would need kernel support
	if( pshared )
	{
		errno = ENOSYS;
		return -1;
	}

	__atomic_store_n( PS4_SemCounter( sem ), (int)value, __ATOMIC_SEQ_CST );
	return 0;
}

int __wrap_sem_destroy( sem_t *sem )
{
	(void)sem;
	return 0;
}

int __wrap_sem_post( sem_t *sem )
{
	volatile int *counter = PS4_SemCounter( sem );
	int value = __atomic_load_n( counter, __ATOMIC_RELAXED );

	do
	{
		if( value == 0x7fffffff )
		{
			errno = EOVERFLOW;
			return -1;
		}
	} while( !__atomic_compare_exchange_n( counter, &value, value + 1, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED ));

	return 0;
}

int __wrap_sem_trywait( sem_t *sem )
{
	volatile int *counter = PS4_SemCounter( sem );
	int value = __atomic_load_n( counter, __ATOMIC_RELAXED );

	while( value > 0 )
	{
		if( __atomic_compare_exchange_n( counter, &value, value - 1, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED ))
			return 0;
	}

	errno = EAGAIN;
	return -1;
}

static int PS4_SemExpired( const struct timespec *abstime )
{
	struct timespec now;

	if( clock_gettime( CLOCK_REALTIME, &now ))
		return 0;

	return now.tv_sec > abstime->tv_sec || ( now.tv_sec == abstime->tv_sec && now.tv_nsec >= abstime->tv_nsec );
}

int __wrap_sem_timedwait( sem_t *sem, const struct timespec *abstime )
{
	int i;

	for( i = 0; ; i++ )
	{
		if( !__wrap_sem_trywait( sem ))
			return 0;

		if( abstime && PS4_SemExpired( abstime ))
		{
			errno = ETIMEDOUT;
			return -1;
		}

		if( i >= PS4_SEM_SPIN )
			sceKernelUsleep( PS4_SEM_POLL_USEC );
	}
}

int __wrap_sem_wait( sem_t *sem )
{
	return __wrap_sem_timedwait( sem, NULL );
}

int __wrap_sem_getvalue( sem_t *sem, int *value )
{
	*value = __atomic_load_n( PS4_SemCounter( sem ), __ATOMIC_SEQ_CST );
	return 0;
}
