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
#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>
#include <QVariantMap>
#include <build.h>
#include "engine.h"
#include "gamecover.h"
#include "launcher.h"
#include "megabuild.h"
#include "settings.h"

// these mods work with valve libraries built from hlsdk-portable mobile_hacks branch, see android/.../model/Game.kt
static const char *const s_mobile_hacks_games[] =
{
	"aom", "bdlands", "biglolly", "bshift", "caseclosed", "hl_urbicide", "induction",
	"redempt", "secret", "sewer_beta", "tot", "valve", "vendetta",
};

// the engine's library suffix for this architecture
#if XASH_ARM && XASH_64BIT
#define CLIENT_LIBRARY "cl_dlls/client_arm64.so"
#elif XASH_ARM
#define CLIENT_LIBRARY "cl_dlls/client_armv7hf.so"
#else
#define CLIENT_LIBRARY ""
#endif

// gameinfo.txt takes priority over liblist.gam, like in the engine
static void ReadGameInfo( const QDir &dir, QString &title, QString &icon )
{
	static const struct
	{
		const char *file;
		const char *title_key;
	} sources[] =
	{
		{ "gameinfo.txt", "title" },
		{ "liblist.gam", "game" },
	};

	const QRegularExpression re( "^\\s*(\\w+)\\s+\"([^\"]*)\"" );

	for( const auto &source : sources )
	{
		QFile file( FindFileCaseInsensitive( dir, source.file ));

		if( !file.open( QIODevice::ReadOnly | QIODevice::Text ))
			continue;

		QTextStream stream( &file );

		while( !stream.atEnd( ))
		{
			const QRegularExpressionMatch match = re.match( stream.readLine( ));

			if( !match.hasMatch( ))
				continue;

			const QString key = match.captured( 1 );

			if( !key.compare( source.title_key, Qt::CaseInsensitive ))
				title = match.captured( 2 );
			else if( !key.compare( "icon", Qt::CaseInsensitive ))
				icon = match.captured( 2 );
		}

		return;
	}
}

// shell-like: whitespace separates, quotes group, backslash escapes
static QStringList SplitArguments( const QString &line )
{
	QStringList args;
	QString current;
	QChar quote;
	bool in_arg = false;

	for( int i = 0; i < line.size(); i++ )
	{
		const QChar c = line[i];

		if( !quote.isNull( ))
		{
			if( c == quote )
				quote = QChar();
			else if( c == '\\' && quote == '"' && i + 1 < line.size( ))
				current += line[++i];
			else
				current += c;
			continue;
		}

		if( c.isSpace( ))
		{
			if( in_arg )
				args << current;

			current.clear();
			in_arg = false;
			continue;
		}

		in_arg = true;

		if( c == '"' || c == '\'' )
			quote = c;
		else if( c == '\\' && i + 1 < line.size( ))
			current += line[++i];
		else
			current += c;
	}

	if( in_arg )
		args << current;

	return args;
}

Launcher::Launcher( QObject *parent ) : QObject( parent )
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	m_baseDir = settings.value( "basedir", defaultBaseDir( )).toString();
	prepareBaseDir();
	refresh();
}

QString Launcher::defaultBaseDir() const
{
	return QDir::home().filePath( ENGINE_BASEDIR );
}

void Launcher::prepareBaseDir()
{
	QDir().mkpath( m_baseDir );

	// otherwise Tracker puts every game image into Gallery
	QFile nomedia( QDir( m_baseDir ).filePath( ".nomedia" ));

	if( !nomedia.exists( ))
		nomedia.open( QIODevice::WriteOnly );
}

void Launcher::setBaseDir( const QString &dir )
{
	const QString path = dir.isEmpty() ? defaultBaseDir() : QDir::cleanPath( dir );

	if( path == m_baseDir )
		return;

	QSettings settings( SettingsPath(), QSettings::IniFormat );

	if( path == defaultBaseDir( ))
		settings.remove( "basedir" );
	else
		settings.setValue( "basedir", path );

	m_baseDir = path;
	prepareBaseDir();
	emit baseDirChanged();
	refresh();
}

void Launcher::refresh()
{
	const QFileInfoList dirs = QDir( m_baseDir ).entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot );

	m_games.clear();

	for( const QFileInfo &info : dirs )
	{
		const QDir dir( info.absoluteFilePath( ));

		if( FindFileCaseInsensitive( dir, "gameinfo.txt" ).isEmpty() && FindFileCaseInsensitive( dir, "liblist.gam" ).isEmpty( ))
			continue;

		QString title, icon = "game.ico";
		ReadGameInfo( dir, title, icon );

		const QString icon_path = FindGameIcon( dir, icon );
		QVariantMap game;

		game["dir"] = info.fileName();
		game["path"] = info.absoluteFilePath();
		game["title"] = title.isEmpty() ? info.fileName() : title;
		game["icon"] = icon_path;
		game["downloaded"] = dir.exists( MEGABUILD_INFO_FILE );
		// put there by the user, not by the downloader
		game["libraries"] = *CLIENT_LIBRARY && !FindFileCaseInsensitive( dir, CLIENT_LIBRARY ).isEmpty();
		m_games.append( game );
	}

	std::sort( m_games.begin(), m_games.end(), []( const QVariant &a, const QVariant &b )
	{
		return a.toMap().value( "title" ).toString().localeAwareCompare( b.toMap().value( "title" ).toString( )) < 0;
	});

	emit gamesChanged();
}

bool Launcher::startLastGame() const
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	return settings.value( "start_last_game", false ).toBool();
}

void Launcher::setStartLastGame( bool enable )
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	settings.setValue( "start_last_game", enable );
	emit startLastGameChanged();
}

QVariantMap Launcher::lastGame() const
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );
	const QString gamedir = settings.value( "last_game" ).toString();

	for( const QVariant &game : m_games )
	{
		const QVariantMap map = game.toMap();

		if( map.value( "dir" ).toString() == gamedir )
			return map;
	}

	return QVariantMap();
}

bool Launcher::isBundled( const QString &gamedir ) const
{
	for( const char *game : s_mobile_hacks_games )
	{
		if( !gamedir.compare( game, Qt::CaseInsensitive ))
			return true;
	}

	return false;
}

QString Launcher::gameArguments( const QString &gamedir ) const
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	return settings.value( "games/" + gamedir + "/arguments", defaultArguments( )).toString();
}

void Launcher::setGameArguments( const QString &gamedir, const QString &args )
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	// the field shows the defaults as a placeholder when it's empty
	if( args.trimmed().isEmpty( ))
		settings.remove( "games/" + gamedir + "/arguments" );
	else
		settings.setValue( "games/" + gamedir + "/arguments", args );
}

void Launcher::launch( const QString &gamedir )
{
	m_engineArgs.clear();

	if( gamedir.compare( "valve", Qt::CaseInsensitive ))
		m_engineArgs << "-game" << gamedir;

	if( isBundled( gamedir ))
		m_engineArgs << "-dll" << "@hl";

	m_engineArgs << SplitArguments( gameArguments( gamedir ));
	m_launchRequested = true;

	QSettings settings( SettingsPath(), QSettings::IniFormat );
	settings.setValue( "last_game", gamedir );

	// the engine is started after the event loop exits and Qt is torn down
	QCoreApplication::quit();
}

QString Launcher::licenseText( const QString &name ) const
{
	if( name.contains( '/' ))
		return QString();

	QFile file( "/usr/share/" XASH_PACKAGE_NAME "/licenses/" + name );

	if( !file.open( QIODevice::ReadOnly | QIODevice::Text ))
		return QString();

	return QString::fromUtf8( file.readAll( ));
}
