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
#import "FtpConnection.h"
#import "FtpServer.h"


@implementation FtpConnection

@synthesize transferMode;
@synthesize currentFile;	
@synthesize currentDir;
@synthesize rnfrFilename;

// ----------------------------------------------------------------------------------------------------------
-(id)initWithAsyncSocket:(AsyncSocket*)newSocket forServer:(id)myServer 
// ----------------------------------------------------------------------------------------------------------
{
	self = [super init ];
	if (self)
	{
		connectionSocket = [newSocket retain ];
		server = myServer;
		[ connectionSocket setDelegate:self ];
		[ connectionSocket writeData:DATASTR(@"220 iosFtp server ready.\r\n") withTimeout:-1 tag:0 ];					// send out the welcome message to the client
		[ connectionSocket readDataToData:[AsyncSocket CRLFData] withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];		// start listening for commands on this connection to client
		dataListeningSocket = nil;
		dataPort=2001;
		transferMode = pasvftp;
		queuedData = [[ NSMutableArray alloc ] init ];								// A buffer for sending data when the connection isn't quite up yet
		self.currentDir = [ server.baseDir copy];												// the working directory for this connection, Starts in the directory the server is set to.  set chroot=true in server code to sandbox in
		currentFile = nil;	
		currentFileHandle = nil;													// not saving to a file yet
		rnfrFilename = nil; 
		
		currentUser = nil;
		NSLog(@"FC: Current Directory starting at %@",currentDir );
	}
	return self;
}
// ----------------------------------------------------------------------------------------------------------
-(void)dealloc
// ----------------------------------------------------------------------------------------------------------
{
	if(connectionSocket)
	{
		[connectionSocket setDelegate:nil];
		[connectionSocket disconnect];
		[connectionSocket release];
	}
	
	if(dataListeningSocket){														// 12/2/10 - think this code  is now redundant, dataListeningSocket, does it do anything anymore...?
		[dataListeningSocket setDelegate:nil];
		[dataListeningSocket disconnect];
		[dataListeningSocket release];
		
	}
	if(dataSocket)
	{
		[dataSocket setDelegate:nil];
		[dataSocket disconnect];
		[dataSocket release];
		
	}
	if(dataConnection)[dataConnection release];
	//	[msgComponents release];
	if(queuedData)[queuedData release];
	if(currentFile)[currentFile release];
	if(currentUser)[currentUser release];
	[currentDir release];
	if(currentFileHandle) [currentFileHandle release];
	
	[super dealloc];
}
#pragma mark STATE 
// ----------------------------------------------------------------------------------------------------------
//-(void)setTransferMode:(int)mode
// ----------------------------------------------------------------------------------------------------------
//{
//	transferMode = mode;
//}

// ----------------------------------------------------------------------------------------------------------
-(NSString*)connectionAddress
// ----------------------------------------------------------------------------------------------------------
{
	return [connectionSocket connectedHost];
	
}


// ==========================================================================================================
#pragma mark CHOOSE DATA SOCKET 
// ==========================================================================================================



// ----------------------------------------------------------------------------------------------------------
-(BOOL)openDataSocket:(int)portNumber
// ----------------------------------------------------------------------------------------------------------
{
	NSString *responseString;
	NSError *error = nil;		
	
	if (dataSocket)																			// Code changed 2010-02-12 - As per Jon's fix. stops memory leak on socket and connection
	{ 
		[dataSocket release]; 
		dataSocket = nil; 
	} 
	dataSocket = [ [ AsyncSocket alloc ] initWithDelegate:self ];                           // 	create our socket for Listening or Direct Connection 
	if (dataConnection) 
	{ 
		[dataConnection release]; 
		dataConnection = nil; 
	} 


	
	switch (transferMode) {
		case portftp:
			dataPort = portNumber;
			responseString = [ NSString stringWithFormat:@"200 PORT command successful."];
			[ dataSocket connectToHost:[self connectionAddress] onPort:portNumber error:&error ];
			//			sleep(1);
			dataConnection = [[ FtpDataConnection alloc ] initWithAsyncSocket:dataSocket forConnection:self withQueuedData:queuedData ];	
			
			break;
			
		case lprtftp:					// FIXME wrong return message
			dataPort = portNumber;
			responseString = [ NSString stringWithFormat:@"228 Entering Long Passive Mode 	(af, hal, h1, h2, h3,..., pal, p1, p2...)", dataPort >>8, dataPort & 0xff];
			[ dataSocket connectToHost:[self connectionAddress] onPort:portNumber error:&error ];
			dataConnection = [[ FtpDataConnection alloc ] initWithAsyncSocket:dataSocket forConnection:self withQueuedData:queuedData ];	
			break;
			
		case eprtftp:
			dataPort = portNumber;
			responseString = @"200 EPRT command successful.";
			[ dataSocket connectToHost:[self connectionAddress] onPort:portNumber error:&error ];
			dataConnection = [[ FtpDataConnection alloc ] initWithAsyncSocket:dataSocket forConnection:self withQueuedData:queuedData ];	
			break;
			
		case pasvftp:
			dataPort = [ self choosePasvDataPort ];
			NSString *address = [ [connectionSocket localHost ] stringByReplacingOccurrencesOfString:@"." withString:@"," ]; // autoreleased
			responseString = [ NSString stringWithFormat:@"227 Entering Passive Mode (%@,%d,%d)",address, dataPort >>8, dataPort & 0xff];				
			[ dataSocket acceptOnPort: dataPort error:&error  ];
			dataConnection = nil;  // will pickup from the listening socket
			break;
			
		case epsvftp:
			dataPort = [ self choosePasvDataPort ];
			responseString = [ NSString stringWithFormat:@"229 Entering Extended Passive Mode (|||%d|)", dataPort ];				
			[ dataSocket acceptOnPort: dataPort error:&error  ];
			
			dataConnection = nil;  // will pickup from the listening socket
			break;
			
			
		default:
			break;
	}
	NSLog( @"-- %@", [ error localizedDescription ] );
	
	[ self sendMessage:responseString ];
	
	return YES;
}


// ----------------------------------------------------------------------------------------------------------
-(int)choosePasvDataPort
// ----------------------------------------------------------------------------------------------------------
{
	struct timeval tv;
    unsigned short int seed[3];
	
    gettimeofday(&tv, NULL);
    seed[0] = (tv.tv_sec >> 16) & 0xFFFF;
    seed[1] = tv.tv_sec & 0xFFFF;
    seed[2] = tv.tv_usec & 0xFFFF;
    seed48(seed);
	
	int portNumber;
	portNumber = (lrand48() % 64512) + 1024;
	//	NSLog(@"New Port number is %i", portNumber );
	
	return portNumber;																// FIXME - presetting to 2001 for the moment
	
	//return 2001;
	
}



// ==========================================================================================================
#pragma mark ASYNCSOCKET DATACONNECTION 
// ==========================================================================================================


// ----------------------------------------------------------------------------------------------------------
-(BOOL)onSocketWillConnect:(AsyncSocket *)sock 
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"FC:onSocketWillConnect");
	[ sock readDataWithTimeout:READ_TIMEOUT tag:0 ];

	return YES;
}

// ----------------------------------------------------------------------------------------------------------
-(void)onSocket:(AsyncSocket *)sock didAcceptNewSocket:(AsyncSocket *)newSocket		// should be for a data connection on 2001
// ----------------------------------------------------------------------------------------------------------
{
	// opened socket for passive data  - new socket connected to this which is the passive connection
	
	NSLog(@"FC:New Connection -- should be for the data port");
	
	dataConnection = [[ FtpDataConnection alloc ] initWithAsyncSocket:newSocket forConnection:self  withQueuedData:queuedData];	
}



// ==========================================================================================================
#pragma mark ASYNCSOCKET FTPCLIENT CONNECTION 
// ==========================================================================================================

// ----------------------------------------------------------------------------------------------------------
-(void)onSocket:(AsyncSocket*)sock didReadData:(NSData*)data withTag:(long)tag			// DATA READ
// ----------------------------------------------------------------------------------------------------------
{
	
	NSLog(@"FC:didReadData");
	[ connectionSocket readDataToData:[AsyncSocket CRLFData] withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];		// start reading again
	
	[ self processDataRead:data ];

}


// ----------------------------------------------------------------------------------------------------------
-(void)onSocket:(AsyncSocket*)sock didWriteDataWithTag:(long)tag						// DATA WRITTEN
// ----------------------------------------------------------------------------------------------------------
{
	[ connectionSocket readDataToData:[AsyncSocket CRLFData] withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];	// start reading again	
	//	NSLog(@"FC:didWriteData");
	
}

// ----------------------------------------------------------------------------------------------------------
-(void)sendMessage:(NSString*)ftpMessage				// REDUNDANT really - FIXME
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@">%@",ftpMessage );
	NSMutableData *dataString = [[ ftpMessage dataUsingEncoding:NSUTF8StringEncoding ] mutableCopy];				// Autoreleased
	[ dataString appendData:[AsyncSocket CRLFData] ];												
	
	[ connectionSocket writeData:dataString withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];
	[dataString release];
	[ connectionSocket readDataToData:[AsyncSocket CRLFData] withTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];	// start reading again		
	//	[ connectionSocket readDataWithTimeout:READ_TIMEOUT tag:FTP_CLIENT_REQUEST ];
}
// ----------------------------------------------------------------------------------------------------------
-(void)sendDataString:(NSString*)dataString
// ----------------------------------------------------------------------------------------------------------
{
	NSMutableString *message = [[NSMutableString alloc] initWithString:dataString];
	CFStringNormalize((CFMutableStringRef)message, kCFStringNormalizationFormC);
	NSMutableData *data = [[ message dataUsingEncoding:server.clientEncoding ] mutableCopy];				// Autoreleased
	[message release];

	if (dataConnection )
	{	NSLog(@"FC:sendData");
		[ dataConnection writeData:data ];
		
	}
	else
	{
		[ queuedData addObject:data ];
	}
	[data release];
	
}
// ----------------------------------------------------------------------------------------------------------
-(void)sendData:(NSMutableData*)data
// ----------------------------------------------------------------------------------------------------------
{
	if (dataConnection )
	{	NSLog(@"FC:sendData");
		[ dataConnection writeData:data ];
	}
	else
	{
		[ queuedData addObject:data ];
	}
	
}
// ----------------------------------------------------------------------------------------------------------
-(void)didReceiveDataWritten			// notification from FtpDataConnection that the dataWasWritten
// ----------------------------------------------------------------------------------------------------------
{
	
	
	
	NSLog(@"SENDING COMPLETED");
	
	[ self sendMessage:@"226 Transfer complete." ]; // send completed message to client
	[ dataConnection closeConnection ];		
}

// ----------------------------------------------------------------------------------------------------------
-(void)didReceiveDataRead			// notification from FtpDataConnection that the dataWasWritten
// ----------------------------------------------------------------------------------------------------------
{
	
	NSLog(@"FC:didReceiveDataRead");
	
	// must have sent a file
	
	// Should start writing file out if not written yet : FIXNOW
	
	if ( currentFileHandle != nil )
	{
		// Append data on
		NSLog(@"FC:Writing File to %@", currentFile );
		[ currentFileHandle writeData:dataConnection.receivedData ];
		
	}
	else
	{
		NSLog(@"Couldnt write data");
	}
	
	
	
}

// ----------------------------------------------------------------------------------------------------------
-(void)didFinishReading							// Called at the end of a data connection from the client we presume
// ----------------------------------------------------------------------------------------------------------
{
	if (currentFile)
	{
		
		NSLog(@"Closing File Handle");
		[currentFile release];
		currentFile = nil;
	}
	else
	{
		NSLog(@"FC:Data Sent but not sure where its for ");
	}
	
	[ self sendMessage:@"226 Transfer complete." ]; // send completed message to client
	
	//	[ dataConnection closeConnection ];									// It must be closed, it dropped us
	
	if ( currentFileHandle != nil )
	{
		NSLog(@"Closing File Handle");
		
		[ currentFileHandle closeFile ];								// Close the file handle
		[ currentFileHandle release ];
		currentFileHandle = nil;
		[ server didReceiveFileListChanged];
	}
	
	dataConnection.connectionState = clientQuiet;
	
}


// ==========================================================================================================
#pragma mark PROCESS 
// =========================================================================================================

// ----------------------------------------------------------------------------------------------------------
-(void)processDataRead:(NSData*)data			// convert to commands as Client Connection
// ----------------------------------------------------------------------------------------------------------
{	
	NSData *strData = [data subdataWithRange:NSMakeRange(0, [data length] - 2)];				// remove last 2 chars
	NSString *crlfmessage = [[[NSString alloc] initWithData:strData encoding:server.clientEncoding] autorelease];
	NSString *message;
	
	//	message = [ crlfmessage stringByReplacingOccurrencesOfString:@"\r\n" withString:@""];		// gets autoreleased
	
	message = [ crlfmessage stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet ]];
	NSLog(@"<%@",message );
    msgComponents = [message componentsSeparatedByString:@" "];										// change this to use spaces - for the FTP protocol	
	
	[ self processCommand ];
	
	[connectionSocket readDataToData:[AsyncSocket CRLFData] withTimeout:-1 tag:0 ];					// force to readdata CHECK
}

// ----------------------------------------------------------------------------------------------------------
-(void)processCommand // assumes data has been place in Array msgComponents
// ----------------------------------------------------------------------------------------------------------
{
	NSString *commandString =  [ msgComponents objectAtIndex:0];
	
	if ([ commandString length ] > 0)																// If there is a command here
	{
		// Search through dictionary for correct matching command and method that it calls
		
		NSString *commandSelector = [ [[server commands] objectForKey:[commandString lowercaseString] ] stringByAppendingString:@"arguments:"];
		
		if ( commandSelector )																		// If we have a matching command
		{
			
			SEL action = NSSelectorFromString(commandSelector);										// Turn into a method
			
			if ( [ self respondsToSelector:action ])												// If we respond to this method
			{	
				// DO COMMAND
				[self performSelector:action withObject:self withObject:msgComponents ];			// Preform method with arguments
			}
			else
			{			// UNKNOWN COMMAND
				NSString *outputString =[ NSString stringWithFormat:@"500 '%@': command not understood.", commandString ];
				[ self sendMessage:outputString ];
				
				NSLog(@"DONT UNDERSTAND");
			}
		}
		else			// UNKNOWN COMMAND
		{
			NSString *outputString =[ NSString stringWithFormat:@"500 '%@': command not understood.", commandString ];
			[ self sendMessage:outputString ];
		}
	}
	else
	{
		// Write out an error msg
	}
	
}

// ==========================================================================================================
#pragma mark COMMANDS 
// ==========================================================================================================

// ----------------------------------------------------------------------------------------------------------
-(void)doQuit:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"Quit : %@",arguments);
	
	[ self sendMessage:@"221- Data traffic for this session was 0 bytes in 0 files"];
	
	[self sendMessage:@"221 Thank you for using the FTP service on localhost." ];
	
	if(connectionSocket)
	{
		[connectionSocket disconnectAfterWriting ];				// Will this close the socket ?
		//	[connectionSocket disconnect];
	}
	
	[ server closeConnection:self ];			// Tell the server to close us down, remove us from the list of connections
	
	
	// FIXME - delete the dataconnection if its open
	
	
}
// ----------------------------------------------------------------------------------------------------------
-(void)doUser:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	// send out confirmation message -- 331 password required for
	if ( currentUser != nil )
		[currentUser release];
	currentUser =  [[ arguments objectAtIndex:1 ] retain];
	NSString *outputString = [ NSString stringWithFormat:@"331 Password required for %@", currentUser ];
	[ sender sendMessage:outputString];
}
// ----------------------------------------------------------------------------------------------------------
-(void)doPass:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
//	NSString *pass = [ arguments objectAtIndex:1 ]; 
	NSString *outputString = [ NSString stringWithFormat:@"230 User %@ logged in.", currentUser ];
	[ sender sendMessage:outputString];
}
// ----------------------------------------------------------------------------------------------------------
-(void)doStat:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	// Send out stat message
	[ sender sendMessage:@"211-localhost FTP server status:"];
	// FIXME - add in the stats
	[ sender sendMessage:@"211 End of Status"];
}
// ----------------------------------------------------------------------------------------------------------
-(void)doFeat:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	[ sender sendMessage:@"211-Features supported"];
    
    // If encoding is UTF8, notify the client
    if (server.clientEncoding == NSUTF8StringEncoding)
        [ sender sendMessage:@" UTF8" ];
    
	[ sender sendMessage:@"211 End"];
}




// ----------------------------------------------------------------------------------------------------------
-(void)doList:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	// Get the name of any additional directory that we are asked to list,  if its empty we use the current directory
	
	NSString *lsDir = [ self fileNameFromArgs:arguments] ;				// autoreleased,  get the directory that we're being asked for
	NSString *listText;
	

	if ([lsDir length]<1) {
		lsDir = currentDir;
	}
	else {
		lsDir = [self rootedPath:lsDir ];
	}

	

	NSLog( @"doList currentDir(%@) changeRoot%d", lsDir, server.changeRoot );
	NSLog(@"Will list %@ ",lsDir);
	listText = [ [ server createList:lsDir] retain ];						// Can list directory so do it	
//	if ([self canChangeDirectoryTo:lsDir]) {									
//		listText = [ [ server createList:lsDir] retain ];						// Can list directory so do it
//	}
//	else {
//		listText = @"";															// return nothing as not in chroot
//	}
	
	NSLog( @"doList sending this. %@", listText );
																	
	[ sender sendMessage:@"150 Opening ASCII mode data connection for '/bin/ls'."];	

	[ sender sendDataString:listText  ];	
	[listText release ];
}
// ----------------------------------------------------------------------------------------------------------
-(void)doPwd:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	
	NSLog(@"Will PWD %@ ",[self visibleCurrentDir]);
	
	// CHECKME - changed to show basedir - currentdir
	

	NSString *cmdString = [ NSString stringWithFormat:@"257 \"%@\" is the current directory.", [self visibleCurrentDir] ]; // autoreleased
	[ sender sendMessage:cmdString ];			// FIXME - seems to be buggy on ftp command line client
	
}
// ----------------------------------------------------------------------------------------------------------
-(void)doNoop:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	[sender sendMessage:@"200 NOOP command successful." ];
	
}
// ----------------------------------------------------------------------------------------------------------
-(void)doSyst:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	[ sender sendMessage:@"215 UNIX Type: L8 Version: iosFtp 20080912" ];
}
// ----------------------------------------------------------------------------------------------------------
-(void)doLprt:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{	
	// LPRT,"6,16,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,2,231,92"	
	NSString *socketDesc = [ arguments objectAtIndex:1 ] ;
	NSArray *socketAddr = [ socketDesc componentsSeparatedByString:@"," ];
	
	int hb = [[socketAddr objectAtIndex:19] intValue ];
	int lb = [[socketAddr objectAtIndex:20] intValue ];
	
	NSLog(@"%d %d %d",hb <<8, hb,lb );
	int clientPort = (hb <<8 ) + lb;
	
	[sender setTransferMode:lprtftp];
	
	[ sender openDataSocket:clientPort ];
}
// ----------------------------------------------------------------------------------------------------------
-(void)doEprt:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	// EPRT |2|1080::8:800:200C:417A|5282|
	
	NSString *socketDesc = [ arguments objectAtIndex:1 ] ;
	NSArray *socketAddr = [ socketDesc componentsSeparatedByString:@"|" ];
	
	NSString *item;
	for (item in socketAddr) {
		NSLog(item);
	}
	int clientPort = [[ socketAddr objectAtIndex:3 ] intValue ];
	
	NSLog(@"Got Send Port %d", clientPort );
	
	[sender setTransferMode:eprtftp];
	//	[ sender initDataSocket:clientPort ];
	[ sender openDataSocket:clientPort ];
}


// ----------------------------------------------------------------------------------------------------------
-(void)doPasv:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	[sender setTransferMode:pasvftp];
	//	[ sender initDataSocket:0 ];
	[ sender openDataSocket:0 ];
	
}
// ----------------------------------------------------------------------------------------------------------
-(void)doEpsv:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	// FIXME - open a port random high address
	[sender setTransferMode:epsvftp];
	//	[ sender initDataSocket:0 ];
	[ sender openDataSocket:0 ];
}
// ----------------------------------------------------------------------------------------------------------
-(void)doPort:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	int hb, lb;
	
	//  PORT 127,0,0,1,197,251  looks like this
	// get 2nd argument and split up by , and then take the last 2 bits
	
	NSString *socketDesc = [ arguments objectAtIndex:1 ] ;
	NSArray *socketAddr = [ socketDesc componentsSeparatedByString:@"," ];
	
	hb = [[socketAddr objectAtIndex:4] intValue ];
	lb = [[socketAddr objectAtIndex:5] intValue ];
	
	
	int clientPort = (hb <<8 ) + lb;
	
	
	[sender setTransferMode:portftp];
	
	[ sender openDataSocket:clientPort ];
	
}

// ----------------------------------------------------------------------------------------------------------
-(void)doOpts:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	NSString *cmd = [ arguments objectAtIndex:1 ];
	NSString *cmdstr = [ NSString stringWithFormat:@"502 Unknown command '%@'",cmd ];
	[ sender sendMessage:cmdstr ];
	//	502 Unknown command 'sj'		
}
// ----------------------------------------------------------------------------------------------------------
-(void)doType:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	//		200 Type set to A.
	
	// FIXME - change the data output to the matching type -- we dont do anything with this yet,, there are many types apart from this one.
	
	NSString *cmd =  [ arguments objectAtIndex:1 ];
	
	//if ( [ [cmd lowercaseString] isEqualToString:@"i" ])
	//	{
	NSString *cmdstr = [ NSString stringWithFormat:@"200 Type set to  %@.",cmd ];
	[ sender sendMessage:cmdstr ];
	//	}
	//	else
	//	{
	//		NSString *cmdstr = [ NSString stringWithFormat:@"500 'type %@': command not understood.",cmd ];
	//		[ sender sendMessage:cmdstr ];		
	//	}
}
// ----------------------------------------------------------------------------------------------------------
-(void)doCwd:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	//	250 
	//	500, 501, 502, 421, 530, 550
	//	250 CWD command successful.
	
	NSLog(@"DoCwd arguments is %@",arguments);
	
	NSString *cmdstr;
	NSString *cwdDir = [ self fileNameFromArgs:arguments] ;				// autoreleased,  get the directory that we're being asked for

	if ( [ self changedCurrentDirectoryTo:cwdDir ] )											// tries to change to that directory, checks in bounds and viable
	{
		cmdstr = [ NSString stringWithFormat:@"250 OK. Current directory is %@", [self visibleCurrentDir]];		// currentDir is now in the new place
		cmdstr = @"250 CWD command successful.";
	}
	else
	{
		cmdstr = @"550 CWD failed.";
	}
	
	[ sender sendMessage:cmdstr];
	
	NSLog(@"currentDir is now %@",currentDir );	
	
}
// ----------------------------------------------------------------------------------------------------------
-(void)doNlst:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	[self doList:sender arguments:arguments ];
}
// ----------------------------------------------------------------------------------------------------------
-(void)doStor:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{	
	// eg STOR my filename here.mp3        STOR=0 my=1 filename=2 etc
	
	NSFileManager	*fs = [NSFileManager defaultManager];	
	NSString		*filename = [ self fileNameFromArgs:arguments];		// autoreleased
	NSString		*cmdstr;										

	self.currentFile = [self makeFilePathFrom:filename];														// makes a filepath, using absolute or relative filename
	
	// Check this falls within the area of filesystem we are allowed to write to
	if ( [self validNewFilePath:self.currentFile] )																// FIXME - finish function for test
	{		
		// CREATE and then OPEN NEW FILE FOR WRITING
		if ([fs createFileAtPath:self.currentFile contents:nil attributes:nil]==YES)								
		{
			currentFileHandle = [[ NSFileHandle fileHandleForWritingAtPath:currentFile] retain];				// Open the file handle to write to
			cmdstr   = [ NSString stringWithFormat:@"150 Opening BINARY mode data connection for '%@'.",filename ];	// autoreleased
		}
		else
		{
			// couldn't make file, send out the error
			cmdstr = [ NSString stringWithFormat:@"553 %@: Permission denied.", filename ];

		}
	}
	else
	{
		// couldn't make file as out of root area
		cmdstr = [ NSString stringWithFormat:@"553 %@: Permission denied.", filename ];
	}

	NSLog(@"FC:doStor  %@", currentFile );

	
	[sender sendMessage:cmdstr];
	
																									// CHECKME - data connection should have been brought up by client?
	if (dataConnection )																			// if not we're in trouble. mind u currentFile is doing similar job. 
	{
		NSLog(@"FC:setting connection state to clientSending");
		dataConnection.connectionState = clientSending;
	}
	else
	{
		NSLog(@"FC:Erorr  Cant set connection state to Client Sending : no Connection yet ");
	}
	
	
	
}

// ----------------------------------------------------------------------------------------------------------
-(void)doRetr:(id)sender arguments:(NSArray*)arguments			// DOWNLOAD to CLIENT
// ----------------------------------------------------------------------------------------------------------
{
	BOOL isDir;
	NSString *cmdstr; 
	
	NSString *filename = [self fileNameFromArgs:arguments];		// autoreleased
	NSString *filePath = [self makeFilePathFrom:filename];			// turns relative or absolute path into format we need
	
	NSLog(@"FC:doRetr: %@", filePath );
	
	if ( [self accessibleFilePath:filePath ] )					// if this filepath is in rooted area, and is a real file
	{
		if ( [ [ NSFileManager defaultManager] fileExistsAtPath:filePath isDirectory: &isDir ])			// FIXME - fold into previous line
		{
			if ( isDir ){																				// reject if its a directory request
				[ sender sendMessage: [NSString stringWithFormat:@"550 %@: Not a plain file.",filename]];
			}
			else																						// SEND FILE
			{	// FIXME URGENT - need to stop loading whole file into memory to send
				NSMutableData   *fileData = [[ NSMutableData dataWithContentsOfFile:filePath ] retain];										// FIXME - open in bits ? seems risky opening file in one piece
				cmdstr = [ NSString stringWithFormat:@"150 Opening BINARY mode data connection for '%@'.",filename ];
				[sender sendMessage:cmdstr];
				
				[ sender sendData:fileData  ];																								// Send file
				[fileData release ];
			}		
		}
	}
	else			// doesn't exist or not in basedir sandbox
	{
		cmdstr = [ NSString stringWithFormat:@"50 %@ No such file or directory.",filename ];		
		NSLog(@"FC:doRetr: file %@ doesnt' exist ", filePath);
		[sender sendMessage:cmdstr];
	}


}

// ----------------------------------------------------------------------------------------------------------
-(void)doDele:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	NSString *cmdStr;
	NSError  *error;
	NSString *filename =[self fileNameFromArgs:arguments];		// autoreleased
	NSString *filePath = [self makeFilePathFrom:filename];
	
	NSLog(@"filename is %@",filename);

	// attempt to delete the file
	
	if ( [self accessibleFilePath:filePath ])						// exists and can access
	{				
		if ([[ NSFileManager defaultManager ] removeItemAtPath:filePath error:&error ])
		{
			cmdStr = [ NSString stringWithFormat:@"250 DELE command successful.",filename ];
			[ server didReceiveFileListChanged];				// Addition by Brendan copied in by Rich 16/12/08
		}
		else
		{
			cmdStr = [ NSString stringWithFormat:@"550 DELE command unsuccessful.",filename ];					// FIXME put correct error code in
		}
	}
	else
	{
		cmdStr = [ NSString stringWithFormat:@"550 %@ No such file or directory.", filename];
	}
	
	[ sender sendMessage:cmdStr	];

	
}
// ----------------------------------------------------------------------------------------------------------
-(void)doMlst:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	NSString *filename = [self fileNameFromArgs: arguments];
	NSString *cmdstr = [ NSString stringWithFormat:@"150 Opening BINARY mode data connection for '%@'.",filename ];
	// tell connection to expect a file	
	
	[sender sendMessage:cmdstr];										// FIXME - this doesn't do anything beyond respond with first message
	/*  typiccal output to generate
	 250: MLST 2012.pdf
	 Type=file;Size=2420017;Modify=20080808074805;Perm=adfrw;Unique=AgAADpIHZwA; /Users/monsta/Documents/2012.pdf
	 End
	 */
}
// ----------------------------------------------------------------------------------------------------------
-(void)doSize:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	NSString *cmdStr;
	NSString *filename = [self fileNameFromArgs: arguments];									// Autoreleased
	NSString *filePath = [self makeFilePathFrom:filename];										// Autoreleased  

	if ([self accessibleFilePath:filePath])														// Can we reach that file ?
	{
		if ([self fileSize:filePath] < 10240)													// If small enough for old style size command
		{
			cmdStr = [ NSString stringWithFormat:@"213 %qu",[self fileSize:filePath] ];			// report size
		}
		else
		{
			cmdStr = [ NSString stringWithFormat:@"550 %@ file too large for SIZE.",filename ];
		}		
	}
	else
	{
		cmdStr = [ NSString stringWithFormat:@"550 %@ No such file or directory.",filename ];	// report file not found		
	}	
	
	// tell connection to expect a file	
	
	[sender sendMessage:cmdStr];

}


// ----------------------------------------------------------------------------------------------------------
-(void)doMkdir:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"current dir is %@",currentDir);
	
	NSString *cmdStr;
	NSString		*p  = [self makeFilePathFrom:[self fileNameFromArgs:arguments]];
	NSFileManager	*fs = [NSFileManager defaultManager];
	
	if ( [self validNewFilePath:p ])										// FIXME - make sure function works. see function
	{			
		if( [fs fileExistsAtPath:p isDirectory:nil] )
		{
			cmdStr = [ NSString stringWithFormat:@"Error %@ exists",[self fileNameFromArgs:arguments] ];	
		}
		else
		{
			// FIXME  - check that its ok to make a directory in this position
			
			[fs createDirectoryAtPath:p withIntermediateDirectories:YES attributes:nil error:nil]; 
			cmdStr = [ NSString stringWithFormat:@"250 MKD command successful."];	

		}
	}
	[sender sendMessage:cmdStr];
}

// ----------------------------------------------------------------------------------------------------------
-(void)doCdUp:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	NSLog(@"CurrentDir is %@",[self visibleCurrentDir]);
	
	NSString *upDir=[[self visibleCurrentDir] stringByDeletingLastPathComponent];
	
	
	
	if ( [self changedCurrentDirectoryTo:upDir] )											// checks to see if its ok before moving
	{			
		[sender sendMessage:@"250 CDUP command successful."]; 		
	}
	else
	{
		// create message saying you cant go to that directory - FIXME
		[ sender sendMessage:@"550 CDUP command failed." ];									// CHECKME - look at a typical ftp mkdr command failure message
	}
}

// ----------------------------------------------------------------------------------------------------------
-(void)doRnfr:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{		
	self.rnfrFilename = [self makeFilePathFrom:[self fileNameFromArgs:arguments]];
	
	if ( [ self accessibleFilePath:self.rnfrFilename ] )										// FIXME - finish function
	{
				
		if ( [[ NSFileManager defaultManager] fileExistsAtPath: rnfrFilename ] )
		{
			[ sender sendMessage:@"350 RNFR command successful." ];
		}
		else
			[ sender sendMessage:@"550 RNFR command failed." ];
	}
}

// ----------------------------------------------------------------------------------------------------------
-(void)doRnto:(id)sender arguments:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{	
	// FIXME - check its ok to use the new filename - ie in sandbox/basedir	
	
	if ( self.rnfrFilename == nil ){
		[ sender sendMessage:@"550 RNTO command failed." ];
		return;
	}
	
	NSError *error;
	NSString *rntoFilename = [self makeFilePathFrom:[self fileNameFromArgs:arguments]];
	
	NSLog( rnfrFilename );
	NSLog( rntoFilename );
	
	if ([self validNewFilePath:rntoFilename])													// FIXME - finish function
	{	
		if ( [[ NSFileManager defaultManager] moveItemAtPath:rnfrFilename toPath: rntoFilename error:&error ] ){
				[ server didReceiveFileListChanged];
			[ sender sendMessage:@"250 RNTO command successful." ];
		}
		else{
			NSString *errorString = [error localizedDescription];
			NSLog( @"RNTO failed %@", errorString );
			[ sender sendMessage:@"550 RNTO command failed." ];
		}
//		[rnfrFilename release];
//		rnfrFilename = nil;
//		[rntoFilename release];													// auto released, should be fine to just forget
		
	}
	else
	{
		[ sender sendMessage:@"550 RNTO command failed." ];										// CHECKME - have a look at a typical server response for this error
	}
}

/////////////////////////////////////////
#pragma mark UTILITIES

// ----------------------------------------------------------------------------------------------------------
-(NSString*)makeFilePathFrom:(NSString*)filename
// ----------------------------------------------------------------------------------------------------------
{
	if ( [filename characterAtIndex:0] == '/' )						// if absolute file position
	{
		if ( server.changeRoot )									// then its actually relative to basedir
		{
			return [ [server.baseDir stringByAppendingPathComponent: filename] stringByResolvingSymlinksInPath ];
		}
		else
		{
			return [[filename copy] autorelease ];											// Don't want to hang onto a variable, user should retain if needed.
		}

	}
	else															// add onto the current file postion
	{
		return [ currentDir stringByAppendingPathComponent:filename];					// FIXME / CHECKME - need to set autorelease or retain etc.
	
	}
}

// ----------------------------------------------------------------------------------------------------------
-(unsigned long long)fileSize:(NSString*)filePath
// ----------------------------------------------------------------------------------------------------------
{
	
	
	NSError *error;
	NSDictionary *fileAttribs = [ [ NSFileManager defaultManager ] attributesOfItemAtPath:filePath error:&error ];
	
	NSNumber *fileSize = [ fileAttribs valueForKey:NSFileSize ];
	NSLog(@"File size is %qu ", [fileSize unsignedLongLongValue]);
	return [ fileSize unsignedLongLongValue];
	
}
// ----------------------------------------------------------------------------------------------------------
-(NSString*)fileNameFromArgs:(NSArray*)arguments
// ----------------------------------------------------------------------------------------------------------
{
	NSString *filename = [NSString string];
	NSLog([arguments description ] );
	if ([arguments count] >1) {
		
		
		for ( int n=1; n<[arguments count]; n++) {														// Start at arg2, 
			// test to see if argument begins with a - as it might be just a parameter, not the name
			if (![[[arguments objectAtIndex:n] substringToIndex:1] isEqualToString:@"-"]) 
			{
				if ([filename length]==0) {
					filename = [arguments objectAtIndex:n ];
				}
				else {
					filename = [ NSString stringWithFormat:@"%@ %@", filename, [arguments objectAtIndex:n ] ];						// autoreleased
				}


			}
			else {
				NSLog(@"HYPHEN FOUND IGNORE");
			}


		}
	}
	

	
	return  filename  ;			// autoreleased
}


// ----------------------------------------------------------------------------------------------------------
- (Boolean)changedCurrentDirectoryTo:(NSString *)newDirectory // NOTE - there is probably a unix way of expanding pathnames and taking all the ../. stuff out.   : should look this up 	
// ----------------------------------------------------------------------------------------------------------
{		
	NSFileManager *fileManager = [ NSFileManager defaultManager ];
	
	NSString *currentDirectory = [ fileManager currentDirectoryPath ];  // store current directory
	NSString *testDirectory, *expandedPath;
	

	expandedPath = [[self rootedPath:newDirectory ] retain ];

	// TEST IF ALLOWED TO USE THAT DIRECTORY ( ie is it in allowable filesystem )
	
	if (![self canChangeDirectoryTo:expandedPath]) {
		return false;
	}
	
	
	// CHANGE TO NEW DIRECTORY & GET SYSTEM FILE PATH ( no .. etc )
	if ( ! [ fileManager changeCurrentDirectoryPath:expandedPath ] )	// try changing to new directory
	{
		return false;													// Not a valid directory
	}
	
	testDirectory = [ fileManager currentDirectoryPath ];				// get new directory as string
	

	// CHANGE BACK
	if ( ! [ fileManager changeCurrentDirectoryPath:currentDirectory ] )// Change back to our own directory
	{
		return false;													// Not a valid directory, shouldnt happen, but could u know
	}
	

		  
	self.currentDir = [ testDirectory copy ];								// make a copy, and retain release
	
	[ expandedPath release];
	return true;														// its fine.  would have failed before if not possible
}
// ----------------------------------------------------------------------------------------------------------
-(Boolean)canChangeDirectoryTo:(NSString *)testDirectory // NOTE - there is probably a unix way of expanding pathnames and taking all the ../. stuff out.   : should look this up 	
// ----------------------------------------------------------------------------------------------------------
{
	if (  [server changeRoot] && ( ! [ testDirectory hasPrefix:server.baseDir ]) ) {
		return false;
	}
	else {
		return true;
	}

	
}
// ----------------------------------------------------------------------------------------------------------
- (Boolean)accessibleFilePath:(NSString*)filePath						// checks its a proper name, and also in the chroot sandbox if set
// ----------------------------------------------------------------------------------------------------------
{
	// check, file is accessible within servers rights
	// && file exists
	
	// FIXME - need to expand the filename reference somehow to check where it really is, whether in sandbox. praps look  at tnftpd src code for this.

	return [ [ NSFileManager defaultManager ] fileExistsAtPath:filePath ];
}
// ----------------------------------------------------------------------------------------------------------
- (Boolean)validNewFilePath:(NSString*)filePath
// ----------------------------------------------------------------------------------------------------------
{	
	return true;			// FIXME - check this is within the area we can write to
}
// ----------------------------------------------------------------------------------------------------------
- (NSString *)visibleCurrentDir
// ----------------------------------------------------------------------------------------------------------
{
	
	if ( server.changeRoot )											// if root changed, to basedir
	{
		int alength = [server.baseDir length ];							// length of basedir
		
		NSLog(@"Length is %u", alength );								
		NSString *aString = [ currentDir substringFromIndex:alength ];	// get the bit after basedir
		
		if ( ! [ aString hasSuffix:@"/" ] )								// add a / if needed
		{
			aString = [ aString stringByAppendingString:@"/" ];										
		}
		
		
		// return with basedir prefix removed.
		
		return aString;				// returns just the end part, not the base part
	}
	else
	{
		return currentDir;							// return complete path
	}
	
	
}
// ----------------------------------------------------------------------------------------------------------
-(NSString *)rootedPath:(NSString*)path
// ----------------------------------------------------------------------------------------------------------
{
	NSString  *expandedPath;
	
	// GET FULL PATH OF NEW DIRECTORY
	if ( [ path characterAtIndex:0 ] == '/')					// if its an absolute path
	{
		if ( server.changeRoot )										// if rooted, its a relative path really, so add to basedir
		{
			expandedPath = [ [server.baseDir stringByAppendingPathComponent: path] stringByResolvingSymlinksInPath ];
		}
		else															// it really is an absolute path
		{
			expandedPath = path;								// use the absolute path
		}
	}
	else																// or append onto currentdir ( the one the client chose ), as its a relative path
	{
		expandedPath =[[ currentDir  stringByAppendingPathComponent:path ] stringByResolvingSymlinksInPath];		
	}
	
	expandedPath = [ expandedPath stringByStandardizingPath ];
	
	return expandedPath; // Autoreleased etc
	
}
@end
