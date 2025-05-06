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
	_TASSERT( ( val1 ) != ( val2 ), Msg( S_ERROR "assert failed at %s:%i, \"%d\" != \"%d\"\n", __FILE__, __LINE__, val1, val2 ))
#define TASSERT_EQp( val1, val2 ) \
	_TASSERT( ( val1 ) != ( val2 ), Msg( S_ERROR "assert failed at %s:%i, \"%p\" != \"%p\"\n", __FILE__, __LINE__, val1, val2 ))
#define TASSERT_STR( str1, str2 ) \
	_TASSERT( Q_strcmp(( str1 ), ( str2 )), Msg( S_ERROR "assert failed at %s:%i, \"%s\" != \"%s\"\n", __FILE__, __LINE__, ( str1 ), ( str2 )))

void Test_RunImagelib( void );
void Test_RunLibCommon( void );
void Test_RunCommon( void );
void Test_RunCmd( void );
void Test_RunCvar( void );
void Test_RunCon( void );
void Test_RunVOX( void );
void Test_RunIPFilter( void );
void Test_RunGamma( void );
void Test_RunDelta( void );
void Test_RunBuffer( void );
void Test_RunMunge( void );

#define TEST_LIST_0 \
	Test_RunLibCommon(); \
	Test_RunCommon(); \
	Test_RunCmd(); \
	Test_RunCvar(); \
	Test_RunIPFilter(); \
	Test_RunBuffer(); \
	Test_RunDelta(); \
	Test_RunMunge();

#define TEST_LIST_0_CLIENT \
	Test_RunCon(); \
	Test_RunGamma();

#define TEST_LIST_1 \
	Test_RunImagelib();

#define TEST_LIST_1_CLIENT \
	Test_RunVOX();

#endif

#endif /* TESTS_H */
