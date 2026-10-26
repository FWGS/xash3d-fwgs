/*
crashreports.h - engine crash reports collected by the launcher
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

// the engine writes crash.log and a copy of engine.log into XASH3D_CRASH_DIR,
// the launcher moves them into history on next start, like the Android launcher does
class CrashReports : public QObject
{
	Q_OBJECT
	Q_PROPERTY( QVariantList history READ history NOTIFY historyChanged )

public:
	explicit CrashReports( QObject *parent = nullptr );

	// passed to the engine as XASH3D_CRASH_DIR
	QString pendingDir() const;
	// system information and launch arguments go along with the crash
	void prepareLaunch( const QStringList &args ) const;

	QVariantList history() const;

	// moves a pending crash into history, returns its name or an empty string
	Q_INVOKABLE QString takePending();
	Q_INVOKABLE QString report( const QString &name ) const;
	Q_INVOKABLE QString mailUrl( const QString &name ) const;
	Q_INVOKABLE void remove( const QString &name );

signals:
	void historyChanged();

private:
	QString historyDir() const;
	void prune() const;
};
