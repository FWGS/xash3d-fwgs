/*
crashreports.cpp - engine crash reports collected by the launcher
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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include "crashreports.h"
#include "settings.h"

// from libpublic, the same build number and commit the engine reports
extern "C" int Q_buildnum( void );
extern "C" const char *g_buildcommit;

#define CRASH_EMAIL "crash@fwgs.gay"
#define HISTORY_DAYS 30
#define ENGINE_LOG_LINES 50
#define MAIL_BODY_LIMIT 8000 // mail clients don't reliably take longer mailto links

static QString CrashesDir()
{
	return QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) + "/crashes";
}

static QString ReadFile( const QString &path )
{
	QFile file( path );

	if( !file.open( QIODevice::ReadOnly | QIODevice::Text ))
		return QString();

	return QString::fromUtf8( file.readAll( ));
}

static QString BuildString()
{
	return QString( "%1-%2" ).arg( Q_buildnum( )).arg( QString::fromLatin1( g_buildcommit ));
}

static bool IsValidName( const QString &name )
{
	return name.startsWith( "crash-" ) && !name.contains( '/' ) && !name.contains( ".." );
}

CrashReports::CrashReports( QObject *parent ) : QObject( parent )
{
	prune();
}

QString CrashReports::pendingDir() const
{
	return CrashesDir() + "/pending";
}

QString CrashReports::historyDir() const
{
	return CrashesDir() + "/history";
}

void CrashReports::prune() const
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );
	const QString build = BuildString();

	// reports from another build aren't useful anymore
	if( settings.value( "crashes/last_build" ).toString() != build )
	{
		QDir( CrashesDir( )).removeRecursively();
		settings.setValue( "crashes/last_build", build );
		return;
	}

	const QDateTime limit = QDateTime::currentDateTime().addDays( -HISTORY_DAYS );

	for( const QFileInfo &info : QDir( historyDir( )).entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot ))
	{
		if( info.lastModified() < limit )
			QDir( info.absoluteFilePath( )).removeRecursively();
	}
}

void CrashReports::prepareLaunch( const QStringList &args ) const
{
	QDir dir( pendingDir( ));

	// an unseen crash keeps the information it was collected with
	if( QFileInfo( dir.filePath( "crash.log" )).size() > 0 )
		return;

	dir.mkpath( "." );
	QFile::remove( dir.filePath( "crash.log" ));
	QFile::remove( dir.filePath( "engine.log" ));

	QFile sysinfo( dir.filePath( "sysinfo.txt" ));

	if( sysinfo.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ))
	{
		QTextStream out( &sysinfo );

		out << "Build: " << BuildString() << '\n';
		out << "OS: " << QSysInfo::prettyProductName() << '\n';
		out << "Kernel: " << QSysInfo::kernelVersion() << '\n';
		out << "Architecture: " << QSysInfo::currentCpuArchitecture() << '\n';
		for( const QString &line : ReadFile( "/etc/hw-release" ).split( '\n', QString::SkipEmptyParts ))
		{
			if( !line.startsWith( '#' ))
				out << line << '\n';
		}
	}

	QFile launch( dir.filePath( "launch.txt" ));

	if( launch.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ))
		QTextStream( &launch ) << args.join( ' ' ) << '\n';
}

QVariantList CrashReports::history() const
{
	QVariantList list;

	// names sort by time, newest first
	for( const QFileInfo &info : QDir( historyDir( )).entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed ))
	{
		QVariantMap entry;

		entry["name"] = info.fileName();
		entry["date"] = QFileInfo( QDir( info.absoluteFilePath( )).filePath( "crash.log" )).lastModified();
		list.append( entry );
	}

	return list;
}

QString CrashReports::takePending()
{
	const QDir pending( pendingDir( ));
	const QFileInfo crashlog( pending.filePath( "crash.log" ));

	if( crashlog.size() <= 0 )
		return QString();

	const QString name = "crash-" + crashlog.lastModified().toString( "yyyyMMdd-HHmmss" );

	QDir().mkpath( historyDir( ));

	if( !QDir().rename( pendingDir(), QDir( historyDir( )).filePath( name )))
		return QString();

	emit historyChanged();
	return name;
}

QString CrashReports::report( const QString &name ) const
{
	if( !IsValidName( name ))
		return QString();

	const QDir dir( QDir( historyDir( )).filePath( name ));
	const QString sysinfo = ReadFile( dir.filePath( "sysinfo.txt" ));
	const QString launch = ReadFile( dir.filePath( "launch.txt" ));
	const QStringList enginelog = ReadFile( dir.filePath( "engine.log" )).split( '\n', QString::SkipEmptyParts );
	QString text = ReadFile( dir.filePath( "crash.log" ));

	if( !sysinfo.isEmpty( ))
		text += "\n--- System info ---\n" + sysinfo;

	if( !launch.isEmpty( ))
		text += "\n--- Launch arguments ---\n" + launch;

	if( !enginelog.isEmpty( ))
		text += "\n--- engine.log, last lines ---\n" + enginelog.mid( qMax( 0, enginelog.size() - ENGINE_LOG_LINES )).join( '\n' ) + '\n';

	return text;
}

QString CrashReports::mailUrl( const QString &name ) const
{
	QString body = tr( "Please describe in as much detail as possible what you were doing when the game crashed — every little thing helps us fix it!" ) + "\n\n\n" + report( name );

	// QUrlQuery leaves + and other reserved characters as is, encode everything
	QByteArray encoded = QUrl::toPercentEncoding( body );

	// the limit is on the link, a character takes up to 9 bytes there
	if( encoded.size() > MAIL_BODY_LIMIT )
	{
		const QByteArray marker = QUrl::toPercentEncoding( "\n[truncated]" );

		while( encoded.size() + marker.size() > MAIL_BODY_LIMIT )
		{
			body.chop(( encoded.size() + marker.size() - MAIL_BODY_LIMIT ) / 9 + 1 );
			encoded = QUrl::toPercentEncoding( body );
		}

		encoded += marker;
	}

	return QString( "mailto:" CRASH_EMAIL "?subject=%1&body=%2" )
		.arg( QString::fromLatin1( QUrl::toPercentEncoding( "Xash3D FWGS crash report" )))
		.arg( QString::fromLatin1( encoded ));
}

void CrashReports::remove( const QString &name )
{
	if( !IsValidName( name ))
		return;

	QDir( QDir( historyDir( )).filePath( name )).removeRecursively();
	emit historyChanged();
}
