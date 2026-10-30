/*
updater.h - engine update check against GitHub releases
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
#ifndef UPDATER_H
#define UPDATER_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

class Updater : public QObject
{
	Q_OBJECT
	Q_PROPERTY( int buildNumber READ buildNumber CONSTANT )
	Q_PROPERTY( QString buildCommit READ buildCommit CONSTANT )
	Q_PROPERTY( bool enabled READ enabled CONSTANT )

public:
	explicit Updater( QObject *parent = nullptr );

	int buildNumber() const;
	QString buildCommit() const;
	bool enabled() const { return buildNumber() > 0; }

	// throttled and snoozable, quiet unless there's an update to show
	Q_INVOKABLE void autoCheck();

	// always reports back through checkFinished
	Q_INVOKABLE void manualCheck();
	Q_INVOKABLE void snooze( int remoteBuild );

signals:
	void updateAvailable( int remoteBuild, const QString &url, const QStringList &changelog, int moreCommits );

	// "available", "uptodate", "failed", "disabled"
	void checkFinished( const QString &status );

private:
	void check( bool manual );
	void fetchChangelog( int remoteBuild, const QString &tag, const QString &url );

	QNetworkAccessManager m_network;
};

#endif // UPDATER_H
