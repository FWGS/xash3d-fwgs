/*
gamecover.h - game cover images composed from menu backgrounds
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

#include <QDir>
#include <QQuickImageProvider>

// game data often comes from Windows, where names don't match the case
QString FindFileCaseInsensitive( const QDir &base, const QString &relpath );

// candidates follow the engine and Steam, the largest image wins
QString FindGameIcon( const QDir &game, QString icon );

// image://cover/<absolute game directory path>
class GameCoverProvider : public QQuickImageProvider
{
public:
	GameCoverProvider();

	QImage requestImage( const QString &id, QSize *size, const QSize &requestedSize ) override;
};

// image://icon/<absolute icon path>, takes the largest image from .ico files
class GameIconProvider : public QQuickImageProvider
{
public:
	GameIconProvider();

	QImage requestImage( const QString &id, QSize *size, const QSize &requestedSize ) override;
};
