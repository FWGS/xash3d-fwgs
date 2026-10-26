/*
launcher.h - Sailfish OS launcher backend
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
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Launcher : public QObject
{
	Q_OBJECT
	Q_PROPERTY( QVariantList games READ games NOTIFY gamesChanged )
	Q_PROPERTY( QString baseDir READ baseDir WRITE setBaseDir NOTIFY baseDirChanged )
	Q_PROPERTY( QString defaultBaseDir READ defaultBaseDir CONSTANT )
	Q_PROPERTY( QString defaultArguments READ defaultArguments CONSTANT )
	Q_PROPERTY( bool startLastGame READ startLastGame WRITE setStartLastGame NOTIFY startLastGameChanged )
	Q_PROPERTY( QVariantMap lastGame READ lastGame NOTIFY gamesChanged )

public:
	explicit Launcher( QObject *parent = nullptr );

	QVariantList games() const { return m_games; }
	QString baseDir() const { return m_baseDir; }
	void setBaseDir( const QString &dir );
	QString defaultBaseDir() const;
	QString defaultArguments() const { return "-console -log"; }
	bool startLastGame() const;
	void setStartLastGame( bool enable );
	// empty if it's gone from the base directory
	QVariantMap lastGame() const;

	QStringList engineArgs() const { return m_engineArgs; }
	bool launchRequested() const { return m_launchRequested; }

	Q_INVOKABLE void refresh();
	// bundled mobile_hacks libraries cover it, nothing to download
	Q_INVOKABLE bool isBundled( const QString &gamedir ) const;
	Q_INVOKABLE QString gameArguments( const QString &gamedir ) const;
	Q_INVOKABLE void setGameArguments( const QString &gamedir, const QString &args );
	Q_INVOKABLE void launch( const QString &gamedir );

signals:
	void gamesChanged();
	void baseDirChanged();
	void startLastGameChanged();

private:
	void prepareBaseDir();

	QString m_baseDir;
	QVariantList m_games;
	QStringList m_engineArgs;
	bool m_launchRequested = false;
};
