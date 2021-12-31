#ifndef TESTS_H
#define TESTS_H

#if XASH_ENGINE_TESTS

struct tests_stats_s
{
	uint passed;
	uint failed;
};

extern struct tests_stats_s tests_stats;

#define TRUN( x ) Msg( "Starting " #x "\n" ); \
	x; \
	Msg( "Finished " #x "\n" )

#define TASSERT( exp ) \
	if(!( exp )) \
	{ \
		tests_stats.failed++; \
		Msg( S_ERROR "assert failed at %s:%i\n", __FILE__, __LINE__ ); \
	} \
	else tests_stats.passed++;

#define TASSERT_STR( str1, str2 ) \
	if( Q_strcmp(( str1 ), ( str2 ))) \
	{ \
		tests_stats.failed++; \
		Msg( S_ERROR "assert failed at %s:%i, \"%s\" != \"%s\"\n", __FILE__, __LINE__, ( str1 ), ( str2 )); \
	} \
	else tests_stats.passed++;

void Test_RunImagelib( void );
void Test_RunLibCommon( void );
void Test_RunCommon( void );
void Test_RunCmd( void );
void Test_RunCvar( void );
void Test_RunCon( void );

#endif

#endif /* TESTS_H */
