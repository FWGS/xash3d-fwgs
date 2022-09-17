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

#define _TASSERT( exp, msg ) \
	if( exp ) \
	{ \
		tests_stats.failed++; \
		msg; \
	} \
	else tests_stats.passed++;

#define TASSERT( exp ) \
	_TASSERT( !(exp), Msg( S_ERROR "assert failed at %s:%i\n", __FILE__, __LINE__ ) )
#define TASSERT_EQi( val1, val2 ) \
	_TASSERT( ( val1 ) != ( val2 ), Msg( S_ERROR "assert failed at %s:%i, \"%d\" != \"%d\"\n", __FILE__, __LINE__, #val1, #val2 ))
#define TASSERT_STR( str1, str2 ) \
	_TASSERT( Q_strcmp(( str1 ), ( str2 )), Msg( S_ERROR "assert failed at %s:%i, \"%s\" != \"%s\"\n", __FILE__, __LINE__, ( str1 ), ( str2 )))

void Test_RunImagelib( void );
void Test_RunLibCommon( void );
void Test_RunCommon( void );
void Test_RunCmd( void );
void Test_RunCvar( void );
void Test_RunCon( void );
void Test_RunVOX( void );

#endif

#endif /* TESTS_H */
