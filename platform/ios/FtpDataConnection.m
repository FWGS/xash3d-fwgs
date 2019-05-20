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

#import "FtpDataConnection.h"
#import "FtpConnection.h"


@implementation FtpDataConnection

@synthesize receivedData;
@synthesize connectionState;

// ----------------------------------------------------------------------------------------------------------
-(id)initWithAsyncSocket:(AsyncSocket*)newSocket forConnection:(id)aConnection withQueuedData:(NSMutableArray*)queuedData
// ----------------------------------------------------------------------------------------------------------
{
	self = [super init ];
	if (self)
	{
		dataSocket = [newSocket retain ];						// Hang onto the socket that was generated - the FDC is retained by FC
		ftpConnection = aConnection;
	
		[ dataSocket setDelegate:self ];
		
		if ( [queuedData count ] )
		{
			NSLog(@"FC:Write Queued Data");
			[self writeQueuedData:queuedData ];
			[ queuedData removeAllObjects ];					// Clear out queue
		}
		// [ dataSocket readDataToData:[AsyncSocket CRLFData] withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];
		[ dataSocket readDataWithTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];
		dataListeningSocket = nil;
		receivedData = nil; //	[[ NSMutableData alloc ] init ]   12/nov/08 - no need for this. rcd is just a pointer
		
		connectionState = clientQuiet;						// Nothing coming through
	}
	return self;
}
// ----------------------------------------------------------------------------------------------------------
-(void)dealloc
// ----------------------------------------------------------------------------------------------------------
{

	[dataSocket release];
	[dataListeningSocket release];
	[dataConnection release];
//	[receivedData release];
	
	
	[super dealloc];
}
// ----------------------------------------------------------------------------------------------------------
-(void)writeString:(NSString*)dataString
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"FDC:writeStringData");
	
	NSMutableData *data = [[ dataString dataUsingEncoding:NSUTF8StringEncoding ] mutableCopy];				// Autoreleased
	[ data appendData:[AsyncSocket CRLFData] ];										
	
	[ dataSocket writeData:data withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];		
	[ dataSocket readDataWithTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];
}

// ----------------------------------------------------------------------------------------------------------
-(void)writeData:(NSMutableData*)data
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"FDC:writeData");
//	[ data appendData:[AsyncSocket CRLFData] ];												// Add on CRLF to end of data - as Windows Explorer needs it
	
	connectionState = clientReceiving;														// We hope
	
	[ dataSocket writeData:data withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];	
	
	[ dataSocket readDataWithTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];
}
// ----------------------------------------------------------------------------------------------------------
-(void)writeQueuedData:(NSMutableArray*)queuedData
// ----------------------------------------------------------------------------------------------------------
{
	for (NSMutableData* data in queuedData) {
		[self writeData:data ];
	}
}

// ----------------------------------------------------------------------------------------------------------
-(void)closeConnection
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"FDC:closeConnection");
	[ dataSocket disconnect  ];

}
#pragma mark ASYNCSOCKET DELEGATES
// ----------------------------------------------------------------------------------------------------------
-(BOOL)onSocketWillConnect:(AsyncSocket *)sock 
// ----------------------------------------------------------------------------------------------------------
{

	NSLog(@"FDC:onSocketWillConnect");
	[ dataSocket readDataWithTimeout:READ_TIMEOUT tag:0 ];
	return YES;
}

// ----------------------------------------------------------------------------------------------------------
-(void)onSocket:(AsyncSocket *)sock didAcceptNewSocket:(AsyncSocket *)newSocket
// ----------------------------------------------------------------------------------------------------------
{
	// This shouldnt happen - we should be connected already - and havent set up a listening socket 
	NSLog(@"FDC:New Connection -- shouldn't be called");

}



// ----------------------------------------------------------------------------------------------------------
-(void)onSocket:(AsyncSocket*)sock didReadData:(NSData*)data withTag:(long)tag
// ----------------------------------------------------------------------------------------------------------
{	
	NSLog(@"FDC:didReadData");

//	[ dataSocket readDataToData:[AsyncSocket CRLFData] withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];			// continue reading
	[ dataSocket readDataWithTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];	
	
	
	receivedData = [ data retain];								// make autoreleased copy of data
	
	// notify connection data came through,  ( will write data for us )
	[ftpConnection didReceiveDataRead ];						// notify, the connection, so it knows to write the data
	[ receivedData release ];									// let go, not our business anymore
	connectionState = clientSent;
}


// ----------------------------------------------------------------------------------------------------------
-(void)onSocket:(AsyncSocket*)sock didWriteDataWithTag:(long)tag
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"FDC:didWriteData");
	[ ftpConnection didReceiveDataWritten ];				// notify that we are finished writing
	
//	[ dataSocket readDataToData:[AsyncSocket CRLFData] withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];			// continue reading
	[ dataSocket readDataWithTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];
}


// ----------------------------------------------------------------------------------------------------------
-(void)onSocket:(AsyncSocket *)sock willDisconnectWithError:(NSError *)err
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"FDC:willDisconnect");
	// if we were writing and there's no error,  then it must be the end of file

	if ( connectionState == clientSending )
	{
		NSLog(@"FDC::did FinishReading");
		// hopefully this is the end of the connection. not sure how we can tell
	}
	else
	{
		NSLog(@"FDC: we werent expecting this as we never set clientSending  prob late coming up");
	}
	[ ftpConnection didFinishReading ];																				// its over, please send the message	
}

- (BOOL)onReadStreamEnded:(AsyncSocket*)sock
{
	NSLog( @"FDC: onReadStreamEnded %d(clientSending is %d)", connectionState, clientSending );
	if ( connectionState == clientSent ||
		 connectionState == clientSending ) return YES;
	return NO;
}

@end
