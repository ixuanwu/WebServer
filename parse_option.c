#include "parse.h"

/* parse the command option 
-d(--daemon)   daemon process
-p(--port)     assign http port
-s(--port)     assign https port
-l(--log)      log path
*/

static void usage(void)
{
	fprintf(stderr,"usage:./main [-d --daemon] [-p --port]  [-l --log] [-v --version] [-h --help]\n\n");
	exit(1);
}
static void version(void)
{
	/*fprintf(stdout,"版本:1.0\n功能:web服务器的实现\n\n 
					实现GET,POST两种方式提交的请求处理\n\n
					提供目录访问和简单的访问控制\n\n
					作者:蒋和超\n\n");*/
	exit(1);
}

/* $start parse_option  */
void parse_option(int argc,char *argv[],char* d,char** portp,char **logp)
{
	int opt;
	static char port[16];
	static char log[64];
	struct option longopts[]={
	{"daemon",0,NULL,'d'},   /* 0->hasn't arg   1-> has arg 2->optional arg*/
	{"port",1,NULL,'p'},
	{"log",1,NULL,'l'},
	{"help",0,NULL,'h'},
	{"version",0,NULL,'v'},
	{0,0,0,0}};   /* the last must be a zero array */

	while((opt=getopt_long(argc,argv,":dp:l:hv",longopts,NULL))!=-1)
	{
		switch(opt)
		{
			case 'd':
				*d=1;
				break;
			case 'p':
				strncpy(port,optarg,15);
				*portp=port;
				break;
			case 'l':
				strncpy(log,optarg,63);
				*logp=log;
				break;
			case ':':
				fprintf(stderr,"-%c:option needs a value.\n",optopt);
				exit(1);
				break;
			case 'h':
				usage();
				break;
			case 'v':
				version();
				break;
			case '?':
				fprintf(stderr,"unknown option:%c\n",optopt);
				usage();
				break;
		}
	}
}
/* $end parse_option  */

/* parse_option test
int main(int argc,char **argv)
{

	char d=0,*p=NULL;
	
	parse_option(argc,argv,&d,&p);
	if(d==1)
		printf("daemon\n");
	if(p!=NULL)
		printf("%s\n",p);

}
*/
