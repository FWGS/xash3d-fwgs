/*
messagebox.cpp - engine message boxes shown by the launcher
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
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QRegularExpression>
#include <QScopedPointer>
#include <sailfishapp.h>
#include "messagebox.h"

/*
========================
MessageBox_Main

========================
*/
int MessageBox_Main( int argc, char **argv )
{
	QScopedPointer<QGuiApplication> app( SailfishApp::application( argc, argv ));
	QScopedPointer<QQuickView> view( SailfishApp::createView( ));

	// engine messages carry console color codes
	const QRegularExpression colors( "\\^[0-9]" );
	const QString title = QString::fromUtf8( argc > 1 ? argv[1] : "" ).remove( colors );
	const QString text = QString::fromUtf8( argc > 2 ? argv[2] : "" ).remove( colors ).trimmed();

	QObject::connect( view->engine(), &QQmlEngine::quit, app.data(), &QCoreApplication::quit );
	view->rootContext()->setContextProperty( "messageTitle", title );
	view->rootContext()->setContextProperty( "messageText", text );
	view->setSource( SailfishApp::pathTo( "qml/MessageBox.qml" ));
	view->show();

	return app->exec();
}
