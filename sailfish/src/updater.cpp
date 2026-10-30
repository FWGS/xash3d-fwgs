/*
updater.cpp - engine update check against GitHub releases
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <crtlib.h>
#include "settings.h"
#include "updater.h"

#define RELEASE_API_URL  "https://api.github.com/repos/FWGS/xash3d-fwgs/releases/tags/continuous"
#define COMPARE_API_URL  "https://api.github.com/repos/FWGS/xash3d-fwgs/compare/"
#define RELEASE_PAGE_URL "https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous"

// same rules as the Android launcher
#define STALENESS_DAYS         3  // remote build must be this much newer to be offered
#define MAX_NAG_STALENESS_DAYS 14 // after that, check on every start and ignore snoozing
#define CHECK_INTERVAL_MS      ( 6 * 60 * 60 * 1000LL )
#define SNOOZE_START_MS        ( 12 * 60 * 60 * 1000LL )
#define SNOOZE_ESCALATION_MS   ( 2 * 24 * 60 * 60 * 1000LL ) // snooze shrinks to nothing over this much skipping
#define CHANGELOG_MAX_LINES    15

#if XASH_ARM && XASH_64BIT
#define RPM_ARCH "aarch64"
#elif XASH_ARM
#define RPM_ARCH "armv7hl"
#elif XASH_X86
#define RPM_ARCH "i486"
#else
#define RPM_ARCH "x86_64"
#endif

static qint64 Now()
{
	return QDateTime::currentMSecsSinceEpoch();
}

static QNetworkRequest GitHubRequest( const QString &url )
{
	QNetworkRequest request( url );

	// GitHub API rejects requests without user agent
	request.setRawHeader( "User-Agent", "harbour-xash3d-fwgs" );
	request.setRawHeader( "Accept", "application/vnd.github+json" );
	request.setAttribute( QNetworkRequest::FollowRedirectsAttribute, true );
	return request;
}

// git describe output: [<tag>-<n>-g]<hash>[-dirty]
static QString CommitHash( const QString &describe )
{
	QString hash = describe;

	if( hash.endsWith( "-dirty" ))
		hash.chop( 6 );

	const int pos = hash.lastIndexOf( "-g" );

	if( pos >= 0 )
		hash = hash.mid( pos + 2 );

	return QRegularExpression( "^[0-9a-fA-F]{7,40}$" ).match( hash ).hasMatch() ? hash : QString();
}

Updater::Updater( QObject *parent ) : QObject( parent )
{
}

int Updater::buildNumber() const
{
	return Q_buildnum();
}

QString Updater::buildCommit() const
{
	return QString::fromLatin1( g_buildcommit );
}

void Updater::autoCheck()
{
	if( !enabled( ))
		return;

	QSettings settings( SettingsPath(), QSettings::IniFormat );

	// once the pending update is two weeks newer, the last known remote build skips the interval
	const bool max_nag = settings.value( "update/remote_buildnum", -1 ).toInt() - buildNumber() >= MAX_NAG_STALENESS_DAYS;

	if( !max_nag && Now() - settings.value( "update/last_check_ms", 0 ).toLongLong() < CHECK_INTERVAL_MS )
		return;

	check( false );
}

void Updater::manualCheck()
{
	if( !enabled( ))
	{
		emit checkFinished( "disabled" );
		return;
	}

	check( true );
}

void Updater::check( bool manual )
{
	QNetworkReply *reply = m_network.get( GitHubRequest( RELEASE_API_URL ));

	// Qt 5.6 has no transfer timeout
	QTimer::singleShot( 10000, reply, [reply]() { reply->abort(); } );

	connect( reply, &QNetworkReply::finished, this, [this, reply, manual]()
	{
		QSettings settings( SettingsPath(), QSettings::IniFormat );
		const QJsonObject release = QJsonDocument::fromJson( reply->readAll( )).object();

		reply->deleteLater();
		settings.setValue( "update/last_check_ms", Now( ));

		if( reply->error() != QNetworkReply::NoError || release.isEmpty( ))
		{
			if( manual )
				emit checkFinished( "failed" );
			return;
		}

		const QRegularExpressionMatch match = QRegularExpression( "buildnum\\s+(\\d+)" ).match( release.value( "body" ).toString( ));
		const int remote = match.hasMatch() ? match.captured( 1 ).toInt() : -1;

		if( remote < 0 || remote - buildNumber() < STALENESS_DAYS )
		{
			if( manual )
				emit checkFinished( "uptodate" );
			return;
		}

		settings.setValue( "update/remote_buildnum", remote );

		if( !manual )
		{
			const bool snoozed = settings.value( "update/dismissed_buildnum", -1 ).toInt() >= remote &&
				remote - buildNumber() < MAX_NAG_STALENESS_DAYS &&
				Now() < settings.value( "update/dismiss_until_ms", 0 ).toLongLong();

			if( snoozed )
				return;
		}

		// the package for this device, or the release page if there's none
		QString url = RELEASE_PAGE_URL;

		for( const QJsonValue &value : release.value( "assets" ).toArray( ))
		{
			const QJsonObject asset = value.toObject();

			if( asset.value( "name" ).toString() == "harbour-xash3d-fwgs." RPM_ARCH ".rpm" )
			{
				url = asset.value( "browser_download_url" ).toString();
				break;
			}
		}

		const QString tag = release.value( "tag_name" ).toString( "continuous" );

		if( manual )
			emit checkFinished( "available" );

		fetchChangelog( remote, tag, url );
	});
}

void Updater::fetchChangelog( int remoteBuild, const QString &tag, const QString &url )
{
	const QString hash = CommitHash( buildCommit( ));

	if( hash.isEmpty( ))
	{
		emit updateAvailable( remoteBuild, url, QStringList(), 0 );
		return;
	}

	QNetworkReply *reply = m_network.get( GitHubRequest( COMPARE_API_URL + hash + "..." + tag ));

	QTimer::singleShot( 10000, reply, [reply]() { reply->abort(); } );

	connect( reply, &QNetworkReply::finished, this, [this, reply, remoteBuild, url]()
	{
		const QJsonObject compare = QJsonDocument::fromJson( reply->readAll( )).object();
		QStringList subjects;

		reply->deleteLater();

		for( const QJsonValue &value : compare.value( "commits" ).toArray( ))
		{
			const QString subject = value.toObject().value( "commit" ).toObject().value( "message" ).toString().section( '\n', 0, 0 ).trimmed();

			// GitHub returns the oldest first
			if( !subject.isEmpty( ))
				subjects.prepend( subject );
		}

		const int total = qMax( compare.value( "total_commits" ).toInt(), subjects.size( ));
		const int more = qMax( 0, total - CHANGELOG_MAX_LINES );

		emit updateAvailable( remoteBuild, url, subjects.mid( 0, CHANGELOG_MAX_LINES ), more );
	});
}

void Updater::snooze( int remoteBuild )
{
	QSettings settings( SettingsPath(), QSettings::IniFormat );
	const qint64 now = Now();
	const int prev_build = settings.value( "update/dismissed_buildnum", -1 ).toInt();
	const qint64 prev_first = settings.value( "update/first_dismiss_ms", 0 ).toLongLong();

	// a newer build restarts the escalation, the same build keeps counting
	const qint64 first = ( remoteBuild > prev_build || !prev_first ) ? now : prev_first;

	// 12 hours at first, linearly down to nothing after 2 days of skipping
	const double factor = qBound( 0.0, 1.0 - double( now - first ) / SNOOZE_ESCALATION_MS, 1.0 );

	settings.setValue( "update/dismissed_buildnum", remoteBuild );
	settings.setValue( "update/first_dismiss_ms", first );
	settings.setValue( "update/dismiss_until_ms", now + qint64( SNOOZE_START_MS * factor ));
}
