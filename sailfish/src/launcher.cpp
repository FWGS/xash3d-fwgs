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
#include <crtlib.h>
#include "engine.h"
#include "gamecover.h"
#include "launcher.h"
#include "megabuild.h"
#include "settings.h"

// these mods work with libraries built from hlsdk-portable mobile_hacks branch, see android/.../model/Game.kt
static const char *const s_mobile_hacks_games[] =
{
	"aom", "bdlands", "biglolly", "bshift", "caseclosed", "hl_urbicide", "induction",
	"redempt", "secret", "sewer_beta", "tot", "valve", "vendetta",
};

/*
========================
ReadGameInfo

gameinfo.txt takes priority over liblist.gam, like in the engine
========================
*/
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

/*
========================
SplitArguments

the same rules as the engine console, but commas and apostrophes stay in the words
========================
*/
static QStringList SplitArguments( const QString &line )
{
	QByteArray data = line.toUtf8();
	QByteArray token( data.size() + 1, '\0' );
	QStringList args;
	char *p = data.data();

	while(( p = COM_ParseFileSafe( p, token.data(), token.size(), PFILE_NO_BRACKETS_AS_TOKEN | PFILE_NO_COMMA_AS_TOKEN | PFILE_NO_SINGLE_QUOTE_AS_TOKEN, NULL, NULL )))
		args << QString::fromUtf8( token.constData( ));

	return args;
}

/*
========================
Launcher::Launcher

========================
*/
Launcher::Launcher( QObject *parent ) : QObject( parent )
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	m_baseDir = settings.value( "basedir", defaultBaseDir( )).toString();
	prepareBaseDir();
	refresh();
}

/*
========================
Launcher::defaultBaseDir

========================
*/
QString Launcher::defaultBaseDir() const
{
	return QDir::home().filePath( ENGINE_BASEDIR );
}

/*
========================
Launcher::prepareBaseDir

========================
*/
void Launcher::prepareBaseDir()
{
	QDir().mkpath( m_baseDir );

	// otherwise Tracker puts every game image into Gallery
	QFile nomedia( QDir( m_baseDir ).filePath( ".nomedia" ));

	if( !nomedia.exists( ))
		nomedia.open( QIODevice::WriteOnly );
}

/*
========================
Launcher::setBaseDir

========================
*/
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

/*
========================
Launcher::refresh

========================
*/
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
		m_games.append( game );
	}

	std::sort( m_games.begin(), m_games.end(), []( const QVariant &a, const QVariant &b )
	{
		return a.toMap().value( "title" ).toString().localeAwareCompare( b.toMap().value( "title" ).toString( )) < 0;
	});

	emit gamesChanged();
}

/*
========================
Launcher::startLastGame

========================
*/
bool Launcher::startLastGame() const
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	return settings.value( "start_last_game", false ).toBool();
}

/*
========================
Launcher::setStartLastGame

========================
*/
void Launcher::setStartLastGame( bool enable )
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	settings.setValue( "start_last_game", enable );
	emit startLastGameChanged();
}

/*
========================
Launcher::lastGame

========================
*/
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

/*
========================
Launcher::isBundled

========================
*/
bool Launcher::isBundled( const QString &gamedir ) const
{
	for( const char *game : s_mobile_hacks_games )
	{
		if( !gamedir.compare( game, Qt::CaseInsensitive ))
			return true;
	}

	return false;
}

/*
========================
Launcher::gameArguments

========================
*/
QString Launcher::gameArguments( const QString &gamedir ) const
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	return settings.value( "games/" + gamedir + "/arguments", defaultArguments( )).toString();
}

/*
========================
Launcher::setGameArguments

========================
*/
void Launcher::setGameArguments( const QString &gamedir, const QString &args )
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );

	// the field shows the defaults as a placeholder when it's empty
	if( args.trimmed().isEmpty( ))
		settings.remove( "games/" + gamedir + "/arguments" );
	else
		settings.setValue( "games/" + gamedir + "/arguments", args );
}

/*
========================
Launcher::launch

========================
*/
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

/*
========================
Launcher::licenseText

========================
*/
QString Launcher::licenseText( const QString &name ) const
{
	if( name.contains( '/' ))
		return QString();

	QFile file( "/usr/share/" XASH_PACKAGE_NAME "/licenses/" + name );

	if( !file.open( QIODevice::ReadOnly | QIODevice::Text ))
		return QString();

	return QString::fromUtf8( file.readAll( ));
}
