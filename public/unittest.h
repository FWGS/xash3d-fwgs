/*
unittest.h - simple unnamed unit testing framework
Copyright (C) 2019 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef UNITTEST_H
#define UNITTEST_H

#include <stdio.h>

struct unittest_s;
typedef void (*pfnTest_t)( struct unittest_s *_self );
typedef struct unittest_s
{
	int status; // 0 if succeeded otherwise failed, you can use it as return code
	const char *name;
	pfnTest_t pfn;
	struct unittest_s *next;
} unittest_t;

#define TEST_FIRST_FUNC3(_name, _pfn, _readname) \
	static unittest_t _name = { 0, ( _readname ), ( _pfn ), NULL }

#define TEST_FIRST_FUNC2(_name, _pfn) \
	TEST_FIRST_FUNC3(_name, _pfn, #_name)

#define TEST_FUNC4(_name, _prevname, _pfn, _readname) \
	static unittest_t _name = { 0, ( _readname ), ( _pfn ), &( _prevname ) }

#define TEST_FUNC3(_name, _prevname, _pfn) \
	TEST_FUNC4(_name, _prevname, _pfn, #_name)

#define DECLARE_TEST_FUNC(_name) \
	static void fn ## _name( struct unittest_s *_self )

#define TEST3(_name, _prevname, _readname) \
	DECLARE_TEST_FUNC(_name); \
	TEST_FUNC4(_name, _prevname, fn ## _name, _readname ); \
	DECLARE_TEST_FUNC(_name)
	// {
	// 	your code goes here;
	// }

#define TEST(_name, _prevname) \
	TEST3(_name, _prevname, #_name)

#define TEST_FIRST2(_name, _readname) \
	DECLARE_TEST_FUNC(_name); \
	TEST_FIRST_FUNC3(_name, fn ## _name, _readname); \
	DECLARE_TEST_FUNC(_name)

#define TEST_FIRST(_name) \
	TEST_FIRST2(_name, #_name)

#define IMPLEMENT_RUNNER(_fnname, _lasttest, _msgfunc, _msg ) \
	void _fnname( void ) \
	{ \
		unittest_t *last = &(_lasttest); \
		int failed = 0, total = 0; \
		_msgfunc( "Starting to test '%s'\n", _msg ); \
		for( ; last; last = last->next ) \
		{ \
			_msgfunc( "Checking '%s'\t\t: ", last->name ); \
			last->pfn( last ); \
			if( last->status ) \
			{ \
				_msgfunc( "FAIL %d\n", last->status ); \
				failed++; \
			} \
			else _msgfunc( "PASS\n" ); \
			total++; \
		} \
		_msgfunc( "Summary: %d failed from %d total\n", failed, total ); \
	}

#define IMPLEMENT_MAIN(_lasttest, _msg) IMPLEMENT_RUNNER(main, _lasttest, printf, _msg)

#endif // UNITTEST_H
