/*
megabuild.cpp - hlsdk-mega-build client
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
#include <string.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <miniz.h>
#include "launcher.h"
#include "megabuild.h"

#define RELEASE_URL "https://github.com/FWGS/hlsdk-mega-build/releases/download/continuous/"
#define MANIFEST_URL RELEASE_URL "manifest.json"
#define MANIFEST_VERSION 1

// hlsdk-mega-build platform keys, the library suffix inside is decided by the build
#if defined( __aarch64__ )
#define PLATFORM "linux-arm64"
#elif defined( __arm__ )
#define PLATFORM "linux-armhf"
#elif defined( __x86_64__ )
#define PLATFORM "linux-amd64"
#elif defined( __i386__ )
#define PLATFORM "linux-i386"
#else
#define PLATFORM ""
#endif

static QString CachePath( const char *name )
{
	const QString dir = QStandardPaths::writableLocation( QStandardPaths::CacheLocation );

	QDir().mkpath( dir );
	return dir + '/' + name;
}

static bool IsValidManifest( const QJsonDocument &doc )
{
	return doc.isObject() && doc.object().value( "version" ).toInt( -1 ) == MANIFEST_VERSION;
}

static QString FileSHA256( const QString &path )
{
	QFile file( path );

	if( !file.open( QIODevice::ReadOnly ))
		return QString();

	QCryptographicHash hash( QCryptographicHash::Sha256 );

	hash.addData( &file );
	return QString::fromLatin1( hash.result().toHex( ));
}

static bool ExtractZip( const QString &zip_path, const QDir &base, const QString &gamedir, QJsonArray &files, QString &error )
{
	mz_zip_archive zip;

	memset( &zip, 0, sizeof( zip ));

	if( !mz_zip_reader_init_file( &zip, QFile::encodeName( zip_path ).constData(), 0 ))
	{
		error = MegaBuild::tr( "Can't open the downloaded archive" );
		return false;
	}

	const mz_uint count = mz_zip_reader_get_num_files( &zip );
	bool ok = true;

	for( mz_uint i = 0; i < count && ok; i++ )
	{
		mz_zip_archive_file_stat stat;

		if( !mz_zip_reader_file_stat( &zip, i, &stat ))
		{
			error = MegaBuild::tr( "Can't read the downloaded archive" );
			ok = false;
			break;
		}

		if( mz_zip_reader_is_file_a_directory( &zip, i ))
			continue;

		// archives have the mod folder at the top, its contents go into the game folder the user has
		const QString name = QString::fromUtf8( stat.m_filename );
		const int slash = name.indexOf( '/' );
		const QString rel = slash > 0 ? name.mid( slash + 1 ) : QString();

		if( rel.isEmpty() || rel.startsWith( '/' ) || rel.split( '/' ).contains( ".." ))
		{
			error = MegaBuild::tr( "Unsafe path in the downloaded archive: %1" ).arg( name );
			ok = false;
			break;
		}

		size_t size = 0;
		void *data = mz_zip_reader_extract_to_heap( &zip, i, &size, 0 );

		if( !data )
		{
			error = MegaBuild::tr( "Can't unpack %1" ).arg( name );
			ok = false;
			break;
		}

		const QByteArray bytes = QByteArray::fromRawData( (const char *)data, (int)size );
		const QString target_rel = gamedir + '/' + rel;
		const QString target = base.filePath( target_rel );
		QSaveFile file( target );

		QDir().mkpath( QFileInfo( target ).absolutePath( ));

		if( file.open( QIODevice::WriteOnly ) && file.write( bytes ) == bytes.size() && file.commit( ))
		{
			QFileDevice::Permissions perms = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther;

			if( rel.endsWith( ".so" ))
				perms |= QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther;

			QFile::setPermissions( target, perms );

			QJsonObject entry;
			entry["path"] = target_rel;
			entry["sha256"] = QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex( ));
			files.append( entry );
		}
		else
		{
			error = file.errorString();
			ok = false;
		}

		mz_free( data );
	}

	mz_zip_reader_end( &zip );
	return ok;
}

MegaBuild::MegaBuild( const Launcher *launcher, QObject *parent ) : QObject( parent ),
	m_launcher( launcher ),
	m_hash( QCryptographicHash::Sha256 )
{
}

void MegaBuild::setBusy( bool busy )
{
	if( m_busy == busy )
		return;

	m_busy = busy;
	emit busyChanged();
}

QJsonObject MegaBuild::readBuildInfo( const QString &gamedir ) const
{
	QFile file( QDir( m_launcher->baseDir( )).filePath( gamedir + "/" MEGABUILD_INFO_FILE ));

	if( !file.open( QIODevice::ReadOnly ))
		return QJsonObject();

	return QJsonDocument::fromJson( file.readAll( )).object();
}

bool MegaBuild::isInstalled( const QString &gamedir ) const
{
	return QFile::exists( QDir( m_launcher->baseDir( )).filePath( gamedir + "/" MEGABUILD_INFO_FILE ));
}

QVariantMap MegaBuild::installedInfo( const QString &gamedir ) const
{
	const QJsonObject info = readBuildInfo( gamedir );

	if( info.isEmpty( ))
		return QVariantMap();

	QVariantMap map = info.toVariantMap();
	map.remove( "files" );
	map["downloaded"] = QFileInfo( QDir( m_launcher->baseDir( )).filePath( gamedir + "/" MEGABUILD_INFO_FILE )).lastModified();
	return map;
}

QJsonObject MegaBuild::findBuild( const QString &gamedir ) const
{
	const QJsonObject mods = m_manifest.value( "mods" ).toObject();

	for( const QString &key : mods.keys( ))
	{
		if( key.compare( gamedir, Qt::CaseInsensitive ))
			continue;

		const QJsonObject build = mods.value( key ).toObject().value( "builds" ).toObject().value( PLATFORM ).toObject();

		if( build.value( "filename" ).toString().isEmpty() || build.value( "sha256" ).toString().isEmpty( ))
			return QJsonObject();

		return build;
	}

	return QJsonObject();
}

bool MegaBuild::updateAvailable( const QString &gamedir ) const
{
	const QJsonObject local = readBuildInfo( gamedir );
	const QJsonObject remote = findBuild( gamedir );

	if( local.isEmpty() || remote.isEmpty( ))
		return false;

	if( local.value( "platform" ).toString() != PLATFORM )
		return true;

	// rebuilt every couple of days with a new hash, the source tree tells if anything changed
	const QString remote_tree = remote.value( "source" ).toObject().value( "tree" ).toString();

	if( !remote_tree.isEmpty( ))
		return remote_tree != local.value( "source" ).toObject().value( "tree" ).toString();

	return remote.value( "sha256" ).toString().compare( local.value( "sha256" ).toString(), Qt::CaseInsensitive ) != 0;
}

QVariantMap MegaBuild::verify( const QString &gamedir ) const
{
	const QDir base( m_launcher->baseDir( ));
	const QJsonObject info = readBuildInfo( gamedir );
	const QJsonArray files = info.value( "files" ).toArray();
	const QString platform = info.value( "platform" ).toString();
	QStringList missing, modified, problems;

	for( const QJsonValue &value : files )
	{
		const QJsonObject entry = value.toObject();
		const QString rel = entry.value( "path" ).toString();
		const QString path = base.filePath( rel );
		const QString name = rel.startsWith( gamedir + '/' ) ? rel.mid( gamedir.size() + 1 ) : rel;

		if( !QFileInfo( path ).isFile( ))
			missing.append( name );
		else if( FileSHA256( path ).compare( entry.value( "sha256" ).toString(), Qt::CaseInsensitive ))
			modified.append( name );
	}

	// a game folder copied from a PC keeps the record of libraries downloaded there
	if( platform != PLATFORM )
		problems.append( tr( "These libraries are built for %1, this device needs %2" ).arg( platform ).arg( PLATFORM ));

	if( files.isEmpty( ))
		problems.append( tr( "The list of installed files is missing or damaged" ));

	if( !missing.isEmpty( ))
		problems.append( tr( "Missing: %1" ).arg( missing.join( ", " )));

	if( !modified.isEmpty( ))
		problems.append( tr( "Changed: %1" ).arg( modified.join( ", " )));

	QVariantMap result;
	result["ok"] = problems.isEmpty();
	result["message"] = problems.isEmpty() ? tr( "All files are intact" ) : problems.join( '\n' );
	return result;
}

void MegaBuild::fetchManifest( std::function<void( bool reachable )> done )
{
	QNetworkRequest request( QUrl( MANIFEST_URL ));

	request.setAttribute( QNetworkRequest::FollowRedirectsAttribute, true );

	QNetworkReply *reply = m_network.get( request );

	// Qt 5.6 has no transfer timeout
	QTimer::singleShot( 15000, reply, [reply]() { reply->abort(); } );

	connect( reply, &QNetworkReply::finished, this, [this, reply, done]()
	{
		reply->deleteLater();

		const QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
		const QJsonDocument doc = QJsonDocument::fromJson( data );

		if( IsValidManifest( doc ))
		{
			QSaveFile cache( CachePath( "hlsdk-manifest.json" ));

			m_manifest = doc.object();

			if( cache.open( QIODevice::WriteOnly ))
			{
				cache.write( data );
				cache.commit();
			}

			done( true );
			return;
		}

		// a fetched manifest of an unknown version is an error even when there's a cached one
		if( data.isEmpty( ))
		{
			QFile cache( CachePath( "hlsdk-manifest.json" ));

			if( cache.open( QIODevice::ReadOnly ))
			{
				const QJsonDocument cached = QJsonDocument::fromJson( cache.readAll( ));

				if( IsValidManifest( cached ))
				{
					m_manifest = cached.object();
					done( true );
					return;
				}
			}
		}

		done( false );
	});
}

void MegaBuild::query( const QString &gamedir )
{
	fetchManifest( [this, gamedir]( bool reachable )
	{
		if( !reachable )
			emit queryFinished( gamedir, "unreachable" );
		else
			emit queryFinished( gamedir, findBuild( gamedir ).isEmpty() ? "missing" : "available" );
	});
}

void MegaBuild::install( const QString &gamedir )
{
	if( m_busy )
		return;

	m_cancelled = false;
	setBusy( true );
	emit progress( 0, -1 );

	fetchManifest( [this, gamedir]( bool reachable )
	{
		if( m_cancelled )
			fail( gamedir, QString( ));
		else if( !reachable )
			fail( gamedir, tr( "Can't reach the game library server" ));
		else
			download( gamedir, true );
	});
}

void MegaBuild::download( const QString &gamedir, bool retry )
{
	const QJsonObject build = findBuild( gamedir );

	if( build.isEmpty( ))
	{
		fail( gamedir, tr( "There are no game libraries for this device" ));
		return;
	}

	m_download = new QFile( CachePath( "download.zip" ), this );

	if( !m_download->open( QIODevice::WriteOnly | QIODevice::Truncate ))
	{
		const QString error = m_download->errorString();

		delete m_download;
		m_download = nullptr;
		fail( gamedir, error );
		return;
	}

	QNetworkRequest request( QUrl( RELEASE_URL + build.value( "filename" ).toString( )));

	request.setAttribute( QNetworkRequest::FollowRedirectsAttribute, true );
	m_hash.reset();
	m_reply = m_network.get( request );

	connect( m_reply, &QNetworkReply::downloadProgress, this, &MegaBuild::progress );

	connect( m_reply, &QNetworkReply::readyRead, this, [this]()
	{
		const QByteArray chunk = m_reply->readAll();

		m_hash.addData( chunk );

		// a full disk would otherwise show up later as a broken archive
		if( m_download->write( chunk ) != chunk.size( ))
			m_reply->abort();
	});

	connect( m_reply, &QNetworkReply::finished, this, [this, gamedir, build, retry]()
	{
		QNetworkReply *reply = m_reply;
		const QByteArray rest = reply->readAll();
		const QString zip_path = m_download->fileName();

		reply->deleteLater();
		m_hash.addData( rest );
		m_download->write( rest );
		m_download->flush();

		const bool written = m_download->error() == QFileDevice::NoError;
		const QString write_error = m_download->errorString();

		m_download->close();
		delete m_download;
		m_download = nullptr;

		if( m_cancelled )
		{
			QFile::remove( zip_path );
			fail( gamedir, QString( ));
			return;
		}

		if( !written )
		{
			QFile::remove( zip_path );
			fail( gamedir, write_error );
			return;
		}

		const bool downloaded = reply->error() == QNetworkReply::NoError;
		const bool verified = downloaded && QString::fromLatin1( m_hash.result().toHex( )) == build.value( "sha256" ).toString().toLower();

		if( verified )
		{
			finishInstall( gamedir, zip_path );
			return;
		}

		QFile::remove( zip_path );

		// the release is deleted and uploaded again every couple of days, a stale manifest points to missing or other files
		if( retry )
		{
			fetchManifest( [this, gamedir]( bool reachable )
			{
				if( m_cancelled )
					fail( gamedir, QString( ));
				else if( !reachable )
					fail( gamedir, tr( "Can't reach the game library server" ));
				else
					download( gamedir, false );
			});
			return;
		}

		fail( gamedir, downloaded ? tr( "The downloaded file is corrupted" ) : reply->errorString( ));
	});
}

void MegaBuild::finishInstall( const QString &gamedir, const QString &zip_path )
{
	const QDir base( m_launcher->baseDir( ));
	const QJsonObject build = findBuild( gamedir );
	const QJsonObject previous = readBuildInfo( gamedir );
	QJsonArray files;
	QString error;

	if( !previous.isEmpty( ))
		removeRecordedFiles( previous );

	const bool extracted = ExtractZip( zip_path, base, gamedir, files, error );

	QFile::remove( zip_path );

	if( !extracted )
	{
		fail( gamedir, error );
		return;
	}

	QJsonObject info;
	info["gamedir"] = gamedir;
	info["platform"] = PLATFORM;
	info["filename"] = build.value( "filename" );
	info["sha256"] = build.value( "sha256" );
	info["source"] = build.value( "source" ).isObject() ? build.value( "source" ) : QJsonObject();
	info["manifest_build"] = m_manifest.value( "build" );
	info["files"] = files;

	QSaveFile file( base.filePath( gamedir + "/" MEGABUILD_INFO_FILE ));

	if( !file.open( QIODevice::WriteOnly ) || file.write( QJsonDocument( info ).toJson( )) < 0 || !file.commit( ))
	{
		fail( gamedir, file.errorString( ));
		return;
	}

	setBusy( false );
	emit installedChanged( gamedir );
	emit installFinished( gamedir, true, QString( ));
}

void MegaBuild::fail( const QString &gamedir, const QString &error )
{
	setBusy( false );
	emit installFinished( gamedir, false, error );
}

void MegaBuild::cancel()
{
	m_cancelled = true;

	if( m_reply )
		m_reply->abort();
}

bool MegaBuild::removeRecordedFiles( const QJsonObject &info ) const
{
	const QDir base( m_launcher->baseDir( ));
	const QString root = base.canonicalPath() + '/';
	QSet<QString> dirs;

	for( const QJsonValue &value : info.value( "files" ).toArray( ))
	{
		const QJsonObject entry = value.toObject();
		const QString rel = entry.value( "path" ).toString();
		const QString sha = entry.value( "sha256" ).toString();

		if( rel.isEmpty() || sha.isEmpty( ))
			continue;

		const QFileInfo target( base.filePath( rel ));

		// refuse to follow paths that escape the base directory
		if( !target.isFile() || !target.canonicalFilePath().startsWith( root ))
			continue;

		// keep files modified by the user
		if( FileSHA256( target.filePath( )).compare( sha, Qt::CaseInsensitive ))
			continue;

		if( QFile::remove( target.filePath( )))
			dirs.insert( target.absolutePath( ));
	}

	QStringList sorted = dirs.toList();

	std::sort( sorted.begin(), sorted.end(), []( const QString &a, const QString &b ) { return a.count( '/' ) > b.count( '/' ); } );

	for( const QString &dir : sorted )
		QDir().rmdir( dir );

	return true;
}

bool MegaBuild::uninstall( const QString &gamedir )
{
	const QJsonObject info = readBuildInfo( gamedir );

	if( info.isEmpty( ))
		return false;

	removeRecordedFiles( info );
	QFile::remove( QDir( m_launcher->baseDir( )).filePath( gamedir + "/" MEGABUILD_INFO_FILE ));
	emit installedChanged( gamedir );
	return true;
}
