enum {
	pasvftp=0,epsvftp,portftp,lprtftp, eprtftp
};

#define DATASTR(args) [ args dataUsingEncoding:NSUTF8StringEncoding ]

#define SERVER_PORT 20000
#define READ_TIMEOUT -1

#define FTP_CLIENT_REQUEST 0

enum {
	
	clientSending=0, clientReceiving=1, clientQuiet=2,clientSent=3
};
