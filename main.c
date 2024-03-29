#include "wrap.h" 
#include "parse.h"

#define PID_FILE  "pid.file"

static void doit(int fd);
static void writePid(int option);
static void get_requesthdrs(rio_t *rp);
static void post_requesthdrs(rio_t *rp,int *length);
static int parse_uri(char *uri, char *filename, char *cgiargs);//parse the request URI
static void serve_static(int fd, char *filename, int filesize);//service for static page
static void serve_dir(int fd,char *filename);//service for dir request
static void get_filetype(const char *filename, char *filetype);//get the mime type
static void get_dynamic(int fd, char *filename, char *cgiargs);//service for get request
static void post_dynamic(int fd, char *filename, int contentLength,rio_t *rp); //service for post request
static void clienterror(int fd, char *cause, char *errnum,char *shortmsg, char *longmsg);//return error page 
static void sigChldHandler(int signo);//child process handle function when child exit
static int isShowdir=1;//check whether show dir 1->show 0->not show

char* cwd;

int main(int argc, char **argv) 
{
    int listenfd,connfd, port,clientlen;
    pid_t pid;
    struct sockaddr_in clientaddr;
    char isdaemon=0,*portp=NULL,*logp=NULL,tmpcwd[MAXLINE];
	//open log system
    openlog(argv[0],LOG_NDELAY|LOG_PID,LOG_DAEMON);	
    cwd=(char*)get_current_dir_name();	
    strcpy(tmpcwd,cwd);
    strcat(tmpcwd,"/");
    /* parse argv */

    parse_option(argc,argv,&isdaemon,&portp,&logp);
	//read config file from config.ini
    portp==NULL ?(port=atoi(Getconfig("http"))) : (port=atoi(portp));
    Signal(SIGCHLD,sigChldHandler);
    /* init log */
    if(logp==NULL) 
    	logp=Getconfig("log");
    initlog(strcat(tmpcwd,logp));
    /* whether show dir */
    if(strcmp(Getconfig("dir"),"no")==0)
    	isShowdir=0;	
    clientlen = sizeof(clientaddr);
    if(isdaemon==1||strcmp(Getconfig("daemon"),"yes")==0)
    	Daemon(1,1);
    writePid(1);
	listenfd = Open_listenfd(port);
    while (1)
    {
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		if(access_ornot(inet_ntoa(clientaddr.sin_addr))==0)
		{
		    clienterror(connfd,"maybe this web server not open to you!" , "403", "Forbidden", "Tiny couldn't read the file");
		    continue;
		}
		
		if((pid=Fork())>0)
		{
			Close(connfd);//close fd in parent
			continue;
		}
		else if(pid==0)
		{
			doit(connfd);
			exit(1);
		}
    }
}
/* $end main */

/*$sigChldHandler to protect zimble process */
static void sigChldHandler(int signo)
{
	Waitpid(-1,NULL,WNOHANG);
	return;
}
/*$end sigChldHandler */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
static void doit(int fd) 
{
    int is_static,contentLength=0,isGet=1;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE],httpspostdata[MAXLINE];
    rio_t rio;
    memset(buf,0,MAXLINE);	
	/* Read request line and headers */
 	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")!=0&&strcasecmp(method,"POST")!=0) 
    { 
       clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }
    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);
    if (lstat(filename, &sbuf) < 0) 
    {
		clienterror(fd, filename, "404", "Not found",
					 "Tiny couldn't find this file");
		return;
    }
	//judge if it is dir
    if(S_ISDIR(sbuf.st_mode)&&isShowdir)
    	serve_dir(fd,filename);
    if (strcasecmp(method, "POST")==0) 
    	isGet=0;
    if (is_static) 
    { /* Serve static content */
		/*
		 * if is not regular file or can not read  return  error page
		 */
		get_requesthdrs(&rio); 
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
		    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't read the file");
		    return;
		}
		serve_static(fd, filename, sbuf.st_size);
    }
    else 
    { /* Serve dynamic content */
		/*
		 * if not regular file or the file can not excute
		 */
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) 
		{
		    clienterror(fd, filename, "403", "Forbidden",
				"Tiny couldn't run the CGI program");
		    return;
		}
		///whether it is get request  
		if(isGet)
		{
			get_requesthdrs(&rio); 
				
			get_dynamic(fd, filename, cgiargs);
		}
		else
		{
			post_requesthdrs(&rio,&contentLength);

			post_dynamic(fd, filename,contentLength,&rio);
		}
    }
}

/*
 * read_requesthdrs - read and parse HTTP request headers one line by one line
 */
/* $begin get_requesthdrs */
static void get_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    writetime();  /* write access time in log file */
    //printf("%s", buf);
    while(strcmp(buf, "\r\n")) 
    {
	Rio_readlineb(rp, buf, MAXLINE);
	writelog(buf);
	//printf("%s", buf);
    }
    return;
}
/* $end get_requesthdrs */

/* $begin post_requesthdrs */
static void post_requesthdrs(rio_t *rp,int *length) 
{
    char buf[MAXLINE];
    char *p;
    Rio_readlineb(rp, buf, MAXLINE);
    writetime();  /* write access time in log file */
    while(strcmp(buf, "\r\n")) 
    {
		Rio_readlineb(rp, buf, MAXLINE);
		/*
		 * compare s1 and s2 the front n bytes 
		 * */
		if(strncasecmp(buf,"Content-Length:",15)==0)
		{
			p=&buf[15];	
			p+=strspn(p," \t");
			*length=atol(p);
		}
		writelog(buf);
		//printf("%s", buf);
	 }
	return;
}
/* $end post_requesthdrs */


/* $begin post_dynamic */
static void serve_dir(int fd,char *filename)
{
	DIR *dp;
	struct dirent *dirp;
    struct stat sbuf;
	struct passwd *filepasswd;
	int num=1;
	char files[MAXLINE],buf[MAXLINE],name[MAXLINE],img[MAXLINE],modifyTime[MAXLINE],dir[MAXLINE];

	char *p;

	/*
	* Start get the dir   
	*/
	p=strrchr(filename,'/');
	++p;
	strcpy(dir,p);
	strcat(dir,"/");
	/* End get the dir */

	if((dp=opendir(filename))==NULL)
		syslog(LOG_ERR,"cannot open dir:%s",filename);
    sprintf(files, "<html><title>Dir Browser</title>");
	sprintf(files,"%s<style type=""text/css""> a:link{text-decoration:none;} </style>",files);
	sprintf(files, "%s<body bgcolor=""ffffff"" font-family=Arial color=#fff font-size=14px>\r\n", files);
	while((dirp=readdir(dp))!=NULL)
	{
		/*
		 * ignore current dir and upper dir
		 */
		if(strcmp(dirp->d_name,".")==0||strcmp(dirp->d_name,"..")==0)
			continue;
		sprintf(name,"%s/%s",filename,dirp->d_name);
		Stat(name,&sbuf);
		filepasswd=getpwuid(sbuf.st_uid);
		if(!strcmp(dirp->d_name,"index.html"))
		{
				
			serve_static(fd,name,sbuf.st_size);	
			exit(0);
		}
		if(S_ISDIR(sbuf.st_mode))
		{
			sprintf(img,"<img src=""dir.png"" width=""24px"" height=""24px"">");
		}
		else if(S_ISFIFO(sbuf.st_mode))
		{
			sprintf(img,"<img src=""fifo.png"" width=""24px"" height=""24px"">");
		}
		else if(S_ISLNK(sbuf.st_mode))
		{
			sprintf(img,"<img src=""link.png"" width=""24px"" height=""24px"">");
		}
		else if(S_ISSOCK(sbuf.st_mode))
		{
			sprintf(img,"<img src=""sock.png"" width=""24px"" height=""24px"">");
		}
		else
			sprintf(img,"<img src=""file.png"" width=""24px"" height=""24px"">");
		sprintf(files,"%s<p><pre>%-2d %s ""<a href=%s%s"">%-15s</a>%-10s%10d %24s</pre></p>\r\n",files,num++,img,dir,dirp->d_name,dirp->d_name,filepasswd->pw_name,(int)sbuf.st_size,timeModify(sbuf.st_mtime,modifyTime));
	}
	closedir(dp);
	sprintf(files,"%s</body></html>",files);
	/* Send response headers to client */
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, strlen(files));
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, "text/html");
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, files, strlen(files));
	exit(0);

}
/* $end serve_dir */


/* $begin post_dynamic */
static void post_dynamic(int fd, char *filename, int contentLength,rio_t *rp)
{
    char buf[MAXLINE],length[32], *emptylist[] = { NULL },data[MAXLINE];
    int p[2];
	sprintf(length,"%d",contentLength);
    memset(data,0,MAXLINE);

    Pipe(p);

    /*       The post data is sended by client,we need to redirct the data to cgi stdin.
    *  	 so, child read contentLength bytes data from fp,and write to p[1];
    *    parent should redirct p[0] to stdin. As a result, the cgi script can
    *    read the post data from the stdin. 
    */

    /* https already read all data ,include post data  by SSL_read() */
   
    if (Fork() == 0)
	{                     /* child  */ 
		Close(p[0]);
		Rio_readnb(rp,data,contentLength);
		Rio_writen(p[1],data,contentLength);
		exit(0)	;
	}
    
    /* Send response headers to client */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n",buf);
    Rio_writen(fd, buf, strlen(buf));
    //Wait(NULL);
    Dup2(p[0],STDIN_FILENO);  /* Redirct p[0] to stdin */
    Close(p[0]);
    Close(p[1]);
    setenv("CONTENT-LENGTH",length , 1); 
    Dup2(fd,STDOUT_FILENO);        /* Redirct stdout to client */ 
	Execve(filename, emptylist, environ); 
}
/* $end post_dynamic */
/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */

/* $begin parse_uri */
static int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;
    char tmpcwd[MAXLINE];
    strcpy(tmpcwd,cwd);
    strcat(tmpcwd,"/");
	/*parse cgi if access cgi*/
    if (!strstr(uri, "cgi-bin")) 
    {  /* Static content */
		strcpy(cgiargs, "");
		strcpy(filename, strcat(tmpcwd,Getconfig("root")));
		strcat(filename, uri);
		if(uri[strlen(uri)-1] == '/')
		    strcat(filename, "home.html");
		return 1;
	}
    else 
    {  /* Dynamic content */
		//get the request parameter
		ptr = index(uri, '?');
		if (ptr) 
		{
			strcpy(cgiargs, ptr+1);
			*ptr = '\0';
		}
		else 
			strcpy(cgiargs, "");
		strcpy(filename, cwd);
		strcat(filename, uri);
		return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
static void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
    /* Send response headers to client */
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);
	/* map file to memory*/
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, srcp, filesize);
    Munmap(srcp, filesize);
}

/*
 * get_filetype - derive file type from file name
 */
static void get_filetype(const char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
		strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".png"))
		strcpy(filetype, "image/png");
    else
		strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin get_dynamic */
void get_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL },httpsbuf[MAXLINE];
    int p[2];

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n",buf);

    Rio_writen(fd, buf, strlen(buf));
	
	if (Fork() == 0) 
	{ /* child */
		/* Real server would set all CGI vars here */
		setenv("QUERY_STRING", cgiargs, 1); 
		Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */
		Execve(filename, emptylist, environ); /* Run CGI program */
	}
    //Wait(NULL); /* Parent waits for and reaps child */
}
/* $end get_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
static void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    sprintf(buf, "%sContent-type: text/html\r\n",buf);
    sprintf(buf, "%sContent-length: %d\r\n\r\n",buf,(int)strlen(body));

    Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */

/* $begin writePid  */ 
/* if the process is running, the interger in the pid file is the pid, else is -1  */
static void writePid(int option)
{
	int pid;
	FILE *fp=Fopen(PID_FILE,"w+");
	if(option)
		pid=(int)getpid();
	else
		pid=-1;
	fprintf(fp,"%d",pid);
	Fclose(fp);
}
 
/* $end writePid  */ 
