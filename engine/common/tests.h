#ifndef TESTS_H
#define TESTS_H

#if XASH_ENGINE_TESTS

struct tests_stats_s
{
	uint passed;
	uint failed;
};

extern struct tests_stats_s tests_stats;

#define TRUN( x ) Msg( "Running " #x "\n" ); x

#define TASSERT( exp ) \
	if(!( exp )) \
	{ \
		tests_stats.failed++; \
		Msg( S_ERROR "assert failed at %s:%i\n", __FILE__, __LINE__ ); \
	} \
	else tests_stats.passed++;

void Test_RunImagelib( void );
void Test_RunLibCommon( void );
void Test_RunCommon( void );
void Test_RunCmd( void );
void Test_RunCvar( void );

#endif

#endif /* TESTS_H */
