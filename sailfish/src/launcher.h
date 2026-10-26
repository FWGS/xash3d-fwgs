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

class Launcher : public QObject
{
	Q_OBJECT
	Q_PROPERTY( QVariantList games READ games NOTIFY gamesChanged )

public:
	explicit Launcher( QObject *parent = nullptr );

	QVariantList games() const { return m_games; }
	QString baseDir() const { return m_baseDir; }
	QStringList engineArgs() const { return m_engineArgs; }
	bool launchRequested() const { return m_launchRequested; }

	Q_INVOKABLE void refresh();
	Q_INVOKABLE void launch( const QString &gamedir );

signals:
	void gamesChanged();

private:
	QString m_baseDir;
	QVariantList m_games;
	QStringList m_engineArgs;
	bool m_launchRequested = false;
};
