#include <stdio.h>
#include "xcf.h"

static int run_case( const char *label, const char *yaml, qboolean expect )
{
	xcf_config_t cfg;

	ParseConfigText( yaml, &cfg );

	if( cfg.collapse_parens != expect )
	{
		fprintf( stderr, "[%s] collapse_parens=%d, expected %d\n  yaml:\n%s\n",
			label, cfg.collapse_parens, expect, yaml );
		return 0;
	}
	return 1;
}

int main( void )
{
	int n = 0;

	if( !run_case( "empty", "", false ))
		return ++n;

	xcf_config_t cfg2;

	ParseConfigText( NULL, &cfg2 );
	if( cfg2.collapse_parens )
		return ++n;

	if( !run_case( "explicit true",
			"# XashCollapseParens: true\n", true ))
		return ++n;

	if( !run_case( "explicit false",
			"# XashCollapseParens: false\n", false ))
		return ++n;

	if( !run_case( "leading ws",
			"   # XashCollapseParens: true\n", true ))
		return ++n;

	if( !run_case( "tabs",
			"#\tXashCollapseParens\t:\ttrue\n", true ))
		return ++n;

	if( !run_case( "unrelated key",
			"# SomeOtherKey: true\nIndentWidth: 4\n", false ))
		return ++n;

	if( !run_case( "in larger config",
			"---\n"
			"Language: Cpp\n"
			"# XashCollapseParens: true\n"
			"IndentWidth: 4\n",
			true ))
		return ++n;

	if( !run_case( "junk value",
			"# XashCollapseParens: maybe\n", false ))
		return ++n;

	if( !run_case( "no colon",
			"# XashCollapseParens true\n", false ))
		return ++n;

	if( !run_case( "last wins",
			"# XashCollapseParens: false\n# XashCollapseParens: true\n", true ))
		return ++n;

	if( !run_case( "no trailing nl",
			"# XashCollapseParens: true", true ))
		return ++n;

	return 0;
}
