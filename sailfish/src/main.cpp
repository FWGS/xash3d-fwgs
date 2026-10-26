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
#include <QQuickView>
#include <QScopedPointer>
#include <sailfishapp.h>
#include "launcher.h"
#include "engine.h"

Q_DECL_EXPORT int main( int argc, char **argv )
{
	// Harbour allows only one executable, so the engine is started from the same one
	if( getenv( ENGINE_ENV ))
		return Engine_Main( argc, argv );

	QScopedPointer<QGuiApplication> app( SailfishApp::application( argc, argv ));
	QScopedPointer<QQuickView> view( SailfishApp::createView( ));
	QScopedPointer<Launcher> launcher( new Launcher );

	view->rootContext()->setContextProperty( "launcher", launcher.data( ));
	view->setSource( SailfishApp::pathToMainQml( ));
	view->show();

	int ret = app->exec();

	if( !launcher->launchRequested( ))
		return ret;

	const QByteArray basedir = launcher->baseDir().toLocal8Bit();
	const QStringList engine_args = launcher->engineArgs();

	std::vector<QByteArray> storage;

	for( const QString &arg : engine_args )
		storage.push_back( arg.toLocal8Bit( ));

	// close our window and the Wayland connection before the engine opens its own
	launcher.reset();
	view.reset();
	app.reset();

	std::vector<char *> args;

	args.push_back( (char *)ENGINE_BINARY );

	for( QByteArray &arg : storage )
		args.push_back( arg.data( ));

	args.push_back( nullptr );

	setenv( ENGINE_ENV, "1", 1 );
	setenv( "XASH3D_BASEDIR", basedir.constData(), 1 );

	// not /proc/self/exe, it points to the booster when launched through it
	execv( ENGINE_BINARY, args.data( ));

	perror( "execv " ENGINE_BINARY );
	return 1;
}
