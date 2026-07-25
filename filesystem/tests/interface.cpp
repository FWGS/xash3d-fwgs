#include "filesystem_test_common.h"
#include "VFileSystem009.h"

int main()
{
	if( !LoadFilesystem() )
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
