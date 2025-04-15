#if 0
#include <sky/sky.h>

class App : public Shared::Application
{
public:
	using Application::Application;
};

void sky_main()
{
	App("test").run();
}

#else

#include "build.h"
#include "common.h"

#include <sky/sky.h>

#define E_GAME "XASH3D_GAME" // default env dir to start from
#ifndef XASH_GAMEDIR
#define XASH_GAMEDIR "valve"
#endif

class XashConsoleDevice : public sky::Console
{
public:
	void write(const std::string& s, Console::Color color) override
	{
		Sys_Print(("^3[sky]^7 " + s).c_str());
	}

	void writeLine(const std::string& s, Console::Color color) override
	{
		write(s + '\n', color);
	}

	void clear() override { /* not supported */ }
	bool isOpened() const override { return false; }

	bool isEnabled() const override { return true; }
	void setEnabled(bool value) override { /* not supported */ }
};

static char szGameDir[128]; // safe place to keep gamedir
static int szArgc;
static char **szArgv;

static void Sys_ChangeGame(const char *progname)
{
	Q_strncpy(szGameDir, progname, sizeof(szGameDir) - 1);
	Host_Shutdown();
	exit(Host_Main(szArgc, szArgv, szGameDir, 1, &Sys_ChangeGame));
}

int main( int argc, char **argv )
{
	sky::Locator<sky::Dispatcher>::Init();
	sky::Locator<sky::CommandProcessor>::Init();
	sky::Locator<sky::Scheduler>::Init();
	sky::Locator<sky::Console>::Set(std::make_shared<XashConsoleDevice>());

	szArgc = argc;
	szArgv = argv;

	const char* game = getenv(E_GAME);

	if (!game)
		game = XASH_GAMEDIR;

	Q_strncpy(szGameDir, game, sizeof(szGameDir));

	return Host_Main(szArgc, szArgv, game, 0, Sys_ChangeGame);
}
#endif
