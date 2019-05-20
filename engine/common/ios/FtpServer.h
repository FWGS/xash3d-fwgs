/*
 iosFtpServer
 Copyright (C) 2008  Richard Dearlove ( monsta )
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#import <UIKit/UIKit.h>


#import "AsyncSocket.h"
#import "FtpDefines.h"
#import "FtpConnection.h"
#import "list.h"

@interface FtpServer : NSObject {
	
	AsyncSocket		*listenSocket;
	NSMutableArray	*connectedSockets;
	id				server;
	id				notificationObject;
	
	int				portNumber;
    id				delegate;	
	
    NSMutableArray *connections;
	
	NSDictionary	*commands;
	NSString		*baseDir;
	Boolean			changeRoot;							// Change root to virtual root ( basedir )
	int				clientEncoding;						// FTP client encoding type
}
- (id)initWithPort:(unsigned)serverPort withDir:(NSString *)aDirectory notifyObject:(id)sender;
- (void)stopFtpServer;

#pragma mark ASYNCSOCKET DELEGATES
- (void)onSocket:(AsyncSocket *)sock didAcceptNewSocket:(AsyncSocket *)newSocket;
- (void)onSocket:(AsyncSocket *)sock didConnectToHost:(NSString *)host port:(UInt16)port;

#pragma mark NOTIFICATIONS
- (void)didReceiveFileListChanged;

#pragma mark CONNECTIONS
- (void)closeConnection:(id)theConnection;
- (NSString *)createList:(NSString *)directoryPath;

@property (readwrite, retain) AsyncSocket *listenSocket;
@property (readwrite, retain) NSMutableArray *connectedSockets;
@property (readwrite, retain) id server;
@property (readwrite, retain) id notificationObject;
@property (readwrite) int portNumber;
@property (readwrite, retain) id delegate;
@property (readwrite, retain) NSMutableArray *connections;
@property (readwrite, retain) NSDictionary *commands;
@property (readwrite, retain) NSString *baseDir;
@property (readwrite) Boolean changeRoot;
@property (readwrite) int clientEncoding;

@end
