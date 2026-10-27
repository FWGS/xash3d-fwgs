/*
gamecover.cpp - game cover images composed from menu backgrounds, and game icons
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
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include "gamecover.h"

QString FindFileCaseInsensitive( const QDir &base, const QString &relpath )
{
	QString path = base.absolutePath();

	for( const QString &part : relpath.split( '/', QString::SkipEmptyParts ))
	{
		const QDir dir( path );

		if( dir.exists( part ))
		{
			path = dir.filePath( part );
			continue;
		}

		QString found;

		for( const QString &entry : dir.entryList( QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden ))
		{
			if( !entry.compare( part, Qt::CaseInsensitive ))
			{
				found = entry;
				break;
			}
		}

		if( found.isEmpty( ))
			return QString();

		path = dir.filePath( found );
	}

	return path;
}

// Qt has no TGA plugin on Sailfish, menu backgrounds are uncompressed or RLE true color images
static QImage LoadTGA( const QString &path )
{
	QFile file( path );

	if( path.isEmpty() || !file.open( QIODevice::ReadOnly ))
		return QImage();

	const QByteArray data = file.readAll();

	if( data.size() < 18 )
		return QImage();

	const uchar *header = (const uchar *)data.constData();
	const int type = header[2];
	const int width = header[12] | header[13] << 8;
	const int height = header[14] | header[15] << 8;
	const int bpp = header[16];
	const bool top_down = header[17] & 0x20;

	if( header[1] != 0 || ( type != 2 && type != 10 ) || ( bpp != 24 && bpp != 32 ) || width <= 0 || height <= 0 )
		return QImage();

	const int bytes = bpp / 8;
	const uchar *src = header + 18 + header[0];
	const uchar *end = header + data.size();
	const int count = width * height;
	QImage image( width, height, QImage::Format_RGB32 );
	int index = 0;

	auto put = [&]( const uchar *pixel )
	{
		const int x = index % width;
		const int y = top_down ? index / width : height - 1 - index / width;

		image.setPixel( x, y, qRgb( pixel[2], pixel[1], pixel[0] ));
		index++;
	};

	while( index < count )
	{
		if( type == 2 )
		{
			if( end - src < bytes )
				return QImage();

			put( src );
			src += bytes;
			continue;
		}

		if( src >= end )
			return QImage();

		const uchar packet = *src++;
		const int run = ( packet & 0x7f ) + 1;

		if( packet & 0x80 )
		{
			if( end - src < bytes )
				return QImage();

			for( int i = 0; i < run && index < count; i++ )
				put( src );

			src += bytes;
		}
		else
		{
			for( int i = 0; i < run && index < count; i++ )
			{
				if( end - src < bytes )
					return QImage();

				put( src );
				src += bytes;
			}
		}
	}

	return image;
}

// resource/BackgroundLayout.txt: "resolution W H", then "<tga> <mode> <x> <y>" entries
static QImage ComposeFromLayout( const QDir &game, const QString &layout_path )
{
	QFile file( layout_path );

	if( layout_path.isEmpty() || !file.open( QIODevice::ReadOnly | QIODevice::Text ))
		return QImage();

	const QStringList tokens = QString::fromLatin1( file.readAll( )).split( QRegularExpression( "\\s+" ), QString::SkipEmptyParts );
	QImage canvas;
	bool drawn = false;

	for( int i = 0; i < tokens.size( ); )
	{
		if( !tokens[i].compare( "resolution", Qt::CaseInsensitive ) && i + 2 < tokens.size( ))
		{
			const int width = tokens[i + 1].toInt();
			const int height = tokens[i + 2].toInt();

			if( width <= 0 || height <= 0 || width > 4096 || height > 4096 )
				return QImage();

			canvas = QImage( width, height, QImage::Format_RGB32 );
			canvas.fill( Qt::black );
			i += 3;
			continue;
		}

		if( i + 3 >= tokens.size( ) || canvas.isNull( ))
			break;

		const QImage tile = LoadTGA( FindFileCaseInsensitive( game, tokens[i] ));

		if( !tile.isNull( ))
		{
			QPainter painter( &canvas );
			painter.drawImage( tokens[i + 2].toInt(), tokens[i + 3].toInt(), tile );
			drawn = true;
		}

		i += 4;
	}

	return drawn ? canvas : QImage();
}

// old games only have the 800x600 tiles: resource/background/800_<row>_<column>_loading.tga
static QImage ComposeFromTiles( const QDir &game )
{
	QImage canvas( 800, 600, QImage::Format_RGB32 );

	canvas.fill( Qt::black );

	QPainter painter( &canvas );
	bool drawn = false;
	int y = 0;

	for( int row = 1; row <= 3; row++ )
	{
		int x = 0;
		int row_height = 0;

		for( char column = 'a'; column <= 'd'; column++ )
		{
			const QString name = QString( "resource/background/800_%1_%2_loading.tga" ).arg( row ).arg( column );
			const QImage tile = LoadTGA( FindFileCaseInsensitive( game, name ));

			if( tile.isNull( ))
			{
				painter.end();
				return QImage();
			}

			painter.drawImage( x, y, tile );
			x += tile.width();
			row_height = qMax( row_height, tile.height( ));
			drawn = true;
		}

		y += row_height;
	}

	painter.end();
	return drawn ? canvas : QImage();
}

GameCoverProvider::GameCoverProvider() : QQuickImageProvider( QQuickImageProvider::Image )
{
}

QImage GameCoverProvider::requestImage( const QString &id, QSize *size, const QSize &requestedSize )
{
	const QDir game( id );
	QImage image = ComposeFromLayout( game, FindFileCaseInsensitive( game, "resource/HD_BackgroundLayout.txt" ));

	if( image.isNull( ))
		image = ComposeFromLayout( game, FindFileCaseInsensitive( game, "resource/BackgroundLayout.txt" ));

	if( image.isNull( ))
		image = ComposeFromTiles( game );

	if( !image.isNull() && requestedSize.width() > 0 )
		image = image.scaledToWidth( requestedSize.width(), Qt::SmoothTransformation );

	if( size )
		*size = image.size();

	return image;
}

static QImage LoadIcon( const QString &path )
{
	if( path.endsWith( ".tga", Qt::CaseInsensitive ))
		return LoadTGA( path );

	QImageReader reader( path );
	QImage image;

	do
	{
		const QImage frame = reader.read();

		// same sized images usually go from fewer to more colors
		if( !frame.isNull() && frame.width() * frame.height() >= image.width() * image.height( ))
			image = frame;
	} while( reader.jumpToNextImage( ));

	return image;
}

QString FindGameIcon( const QDir &game, QString icon )
{
	icon.replace( '\\', '/' );

	// the engine appends .ico when there's no extension and tries the .tga variant first,
	// Steam shows the _big.tga variant in the library
	if( QFileInfo( icon ).suffix().isEmpty( ))
		icon += ".ico";

	const QString base = icon.left( icon.lastIndexOf( '.' ));
	QString best;
	int best_area = 0;

	// mods often point to a file they don't have, or have a better one elsewhere
	for( const QString &name : { base + "_big.tga", base + ".tga", icon, QString( "game.ico" )})
	{
		const QString path = FindFileCaseInsensitive( game, name );

		if( path.isEmpty( ))
			continue;

		const QImage image = LoadIcon( path );

		if( image.width() * image.height() > best_area )
		{
			best = path;
			best_area = image.width() * image.height();
		}
	}

	return best;
}

GameIconProvider::GameIconProvider() : QQuickImageProvider( QQuickImageProvider::Image )
{
}

QImage GameIconProvider::requestImage( const QString &id, QSize *size, const QSize &requestedSize )
{
	QImage image = LoadIcon( id );

	if( !image.isNull() && requestedSize.isValid( ))
		image = image.scaled( requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation );

	if( size )
		*size = image.size();

	return image;
}
