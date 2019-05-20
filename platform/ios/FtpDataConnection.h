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

@class FtpConnection;

@interface FtpDataConnection : NSObject {
	AsyncSocket			*dataSocket;
	FtpConnection		*ftpConnection;						// connection which generated data socket we are tied to
	
	AsyncSocket			*dataListeningSocket;
	id					dataConnection;	
	NSMutableData		*receivedData;
	int					connectionState;
	
}
-(id)initWithAsyncSocket:(AsyncSocket*)newSocket forConnection:(id)aConnection withQueuedData:(NSMutableArray*)queuedData;
-(void)writeString:(NSString*)dataString;
-(void)writeData:(NSMutableData*)data;
-(void)writeQueuedData:(NSMutableArray*)queuedData;
-(void)closeConnection;

#pragma mark ASYNCSOCKET DELEGATES
-(BOOL)onSocketWillConnect:(AsyncSocket *)sock;
-(void)onSocket:(AsyncSocket *)sock didAcceptNewSocket:(AsyncSocket *)newSocket;
-(void)onSocket:(AsyncSocket*)sock didReadData:(NSData*)data withTag:(long)tag;
-(void)onSocket:(AsyncSocket*)sock didWriteDataWithTag:(long)tag;

-(void)onSocket:(AsyncSocket *)sock willDisconnectWithError:(NSError *)err;

@property (readonly) NSMutableData *receivedData;
@property (readwrite) int connectionState;


@end
