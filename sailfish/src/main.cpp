/*
main.cpp - Sailfish OS launcher
Copyright (C) 2026 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>
#include <sailfishapp.h>
#include "crashreports.h"
#include "engine.h"
#include "gamecover.h"
#include "launcher.h"
#include "megabuild.h"
#include "messagebox.h"
#include "updater.h"

/*
========================
main

========================
*/
Q_DECL_EXPORT int main( int argc, char **argv )
{
	// Harbour allows only one executable, so the engine is started from the same one
	if( getenv( ENGINE_ENV ))
		return Engine_Main( argc, argv );

	if( getenv( MESSAGEBOX_ENV ))
		return MessageBox_Main( argc, argv );

	// declared before the view, so QML is gone before the objects it uses
	QScopedPointer<QGuiApplication> app( SailfishApp::application( argc, argv ));
	QScopedPointer<Launcher> launcher( new Launcher );
	QScopedPointer<MegaBuild> megabuild( new MegaBuild( launcher.data( )));
	QScopedPointer<Updater> updater( new Updater );
	QScopedPointer<CrashReports> crashreports( new CrashReports );
	QScopedPointer<QQuickView> view( SailfishApp::createView( ));

	// the engine owns the provider
	view->engine()->addImageProvider( "cover", new GameCoverProvider );
	view->engine()->addImageProvider( "icon", new GameIconProvider );
	view->rootContext()->setContextProperty( "launcher", launcher.data( ));
	view->rootContext()->setContextProperty( "megabuild", megabuild.data( ));
	view->rootContext()->setContextProperty( "updater", updater.data( ));
	view->rootContext()->setContextProperty( "crashReports", crashreports.data( ));
	view->setSource( SailfishApp::pathToMainQml( ));
	view->show();

	int ret = app->exec();

	if( !launcher->launchRequested( ))
		return ret;

	const QByteArray basedir = launcher->baseDir().toLocal8Bit();
	const QStringList engine_args = launcher->engineArgs();
	const QByteArray crashdir = crashreports->pendingDir().toLocal8Bit();

	crashreports->prepareLaunch( engine_args );

	std::vector<QByteArray> storage;

	for( const QString &arg : engine_args )
		storage.push_back( arg.toLocal8Bit( ));

	// close our window and the Wayland connection before the engine opens its own
	view.reset();
	crashreports.reset();
	updater.reset();
	megabuild.reset();
	launcher.reset();
	app.reset();

	std::vector<char *> args;

	args.push_back( (char *)ENGINE_BINARY );

	for( QByteArray &arg : storage )
		args.push_back( arg.data( ));

	args.push_back( nullptr );

	setenv( ENGINE_ENV, "1", 1 );
	setenv( "XASH3D_BASEDIR", basedir.constData(), 1 );
	setenv( "XASH3D_CRASH_DIR", crashdir.constData(), 1 );

	// not /proc/self/exe, it points to the booster when launched through it
	execv( ENGINE_BINARY, args.data( ));

	perror( "execv " ENGINE_BINARY );
	return 1;
}
