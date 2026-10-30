/*
megabuild.h - hlsdk-mega-build client
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
#ifndef MEGABUILD_H
#define MEGABUILD_H

#include <functional>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

// written into the game directory, same as hlsdk-mega-build's scripts/download_mod.py
#define MEGABUILD_INFO_FILE ".mod_build_info.json"

class QFile;
class QNetworkReply;
class Launcher;

// install state is compatible with hlsdk-mega-build's scripts/download_mod.py
class MegaBuild : public QObject
{
	Q_OBJECT
	Q_PROPERTY( bool busy READ busy NOTIFY busyChanged )

public:
	explicit MegaBuild( const Launcher *launcher, QObject *parent = nullptr );

	bool busy() const { return m_busy; }

	Q_INVOKABLE bool isInstalled( const QString &gamedir ) const;
	Q_INVOKABLE QVariantMap installedInfo( const QString &gamedir ) const;
	Q_INVOKABLE bool updateAvailable( const QString &gamedir ) const;
	// { "ok": bool, "message": string }
	Q_INVOKABLE QVariantMap verify( const QString &gamedir ) const;

	// queryFinished( gamedir, "available" | "missing" | "unreachable" )
	Q_INVOKABLE void query( const QString &gamedir );
	Q_INVOKABLE void install( const QString &gamedir );
	Q_INVOKABLE void cancel();
	Q_INVOKABLE bool uninstall( const QString &gamedir );

signals:
	void busyChanged();
	void queryFinished( const QString &gamedir, const QString &status );
	void progress( qint64 received, qint64 total );
	void installFinished( const QString &gamedir, bool success, const QString &error );
	void installedChanged( const QString &gamedir );

private:
	void setBusy( bool busy );
	void fetchManifest( std::function<void( bool reachable )> done );
	QJsonObject findBuild( const QString &gamedir ) const;
	void download( const QString &gamedir, bool retry );
	void finishInstall( const QString &gamedir, const QString &zip_path );
	void fail( const QString &gamedir, const QString &error );
	QJsonObject readBuildInfo( const QString &gamedir ) const;
	// files in keep belong to the build that replaced it
	bool removeRecordedFiles( const QString &gamedir, const QJsonObject &info, const QJsonArray &keep = QJsonArray() ) const;

	const Launcher *m_launcher;
	QNetworkAccessManager m_network;
	QJsonObject m_manifest;
	QPointer<QNetworkReply> m_reply;
	QFile *m_download = nullptr;
	QCryptographicHash m_hash;
	QString m_writeError;
	bool m_busy = false;
	bool m_cancelled = false;
};

#endif // MEGABUILD_H
