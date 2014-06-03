#include "parse.h"
/*
 * 以下是创建守护进程
 */
void init_daemon(void)
{
	int i;
	pid_t pid;
	umask(0);//清除文件模式创建掩码，使新文件的权限位不受原先文件模式创建掩码的权限位的影响
	if(pid=fork())//创建第一个子进程
		exit(0);//父进程终止
	else if(pid<0)//fork失败
		exit(1);
	setsid();//让子进程成为会话首进程

	signal(SIGHUP,SIG_IGN);//忽略SIGHUP信号


	if(pid=fork())
		exit(0);//第一个子进程终止
	else if(pid<0)//fork失败
		exit(1);

	for(i=0;i<NOFILE;++i)
		close(i);//关闭所有打开的文件描述符

	chdir("/");//改变工作目录
}
