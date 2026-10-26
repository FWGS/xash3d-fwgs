/*
launcher.cpp - Sailfish OS launcher backend
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
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QVariantMap>
#include "launcher.h"
#include "engine.h"

// these mods work with valve libraries built from hlsdk-portable mobile_hacks branch, see android/.../model/Game.kt
static const char *const s_mobile_hacks_games[] =
{
	"aom", "bdlands", "biglolly", "bshift", "caseclosed", "hl_urbicide", "induction",
	"redempt", "secret", "sewer_beta", "tot", "valve", "vendetta",
};

static QString ReadGameTitle( const QDir &dir )
{
	// gameinfo.txt takes priority over liblist.gam, like in the engine
	static const struct
	{
		const char *file;
		const char *key;
	} sources[] =
	{
		{ "gameinfo.txt", "title" },
		{ "liblist.gam", "game" },
	};

	for( const auto &source : sources )
	{
		QFile file( dir.filePath( source.file ));

		if( !file.open( QIODevice::ReadOnly | QIODevice::Text ))
			continue;

		const QRegularExpression re( QStringLiteral( "^\\s*%1\\s+\"([^\"]*)\"" ).arg( source.key ), QRegularExpression::CaseInsensitiveOption );
		QTextStream stream( &file );

		while( !stream.atEnd( ))
		{
			const QRegularExpressionMatch match = re.match( stream.readLine( ));

			if( match.hasMatch( ))
				return match.captured( 1 );
		}
	}

	return QString();
}

Launcher::Launcher( QObject *parent ) : QObject( parent ),
	m_baseDir( QDir::home().filePath( ENGINE_BASEDIR ))
{
	QDir().mkpath( m_baseDir );

	// otherwise Tracker puts every game image into Gallery
	QFile nomedia( QDir( m_baseDir ).filePath( ".nomedia" ));

	if( !nomedia.exists( ))
		nomedia.open( QIODevice::WriteOnly );

	refresh();
}

void Launcher::refresh()
{
	const QFileInfoList dirs = QDir( m_baseDir ).entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase );

	m_games.clear();

	for( const QFileInfo &info : dirs )
	{
		const QDir dir( info.absoluteFilePath( ));

		if( !dir.exists( "gameinfo.txt" ) && !dir.exists( "liblist.gam" ))
			continue;

		const QString title = ReadGameTitle( dir );
		QVariantMap game;

		game["dir"] = info.fileName();
		game["title"] = title.isEmpty() ? info.fileName() : title;
		m_games.append( game );
	}

	emit gamesChanged();
}

void Launcher::launch( const QString &gamedir )
{
	m_engineArgs.clear();

	if( gamedir.compare( "valve", Qt::CaseInsensitive ))
		m_engineArgs << "-game" << gamedir;

	for( const char *game : s_mobile_hacks_games )
	{
		if( !gamedir.compare( game, Qt::CaseInsensitive ))
		{
			m_engineArgs << "-dll" << "@hl";
			break;
		}
	}

	m_launchRequested = true;

	// the engine is started after the event loop exits and Qt is torn down
	QCoreApplication::quit();
}
