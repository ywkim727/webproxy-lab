/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

int main(int argc, char **argv)                                         //입력 ./tiny 8000 / argc = 2, argv[0] = tiny, argv[1] = 8000
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {                                                    //포트 번호를 제대로 입력하지 않은 경우
	fprintf(stderr, "usage: %s <port>\n", argv[0]);                     //오류 메시지 출력하고 프로그램 종료
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);                                  //Open_listenfd 함수를 호출하여 듣기 소켓을 오픈 
    while (1) {
	clientlen = sizeof(clientaddr);                                     //반복적으로 연결 요청을 접수
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,   //getaddinfo : 호스트이름, 호스트주소, 서비스이름, 포트번호의 스트링 표시를 소켓주소 구조체로 변환
                    port, MAXLINE, 0);                                  //getnameinfo : 위를 반대로 소켓 주소 구조체에서 스트링 표시로 변환
        printf("Accepted connection from (%s, %s)\n", hostname, port);  
	doit(connfd);                                                       //트랜젝션을 수행
	Close(connfd);                                                      //자신 쪽의 연결 끝을 닫는다
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction                  //doit함수는 한개의 HTTP 트랜젝션을 처리한다
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);       
    if (strcasecmp(method, "GET")) {                        //GET메소드만 지원, 다른 메소드를 요청하면 
        clienterror(fd, method, "501", "Not Implemented",   //에러 메시지를 출력, main루틴으로 돌아오고 그 후에 연결을 닫고 다음 연결 요청을 기다린다
                    "Tiny does not implement this method");
        return;
    }                                                    
    read_requesthdrs(&rio);                                 //에러가 안나면 읽어들이고 다른 헤더들을 무시한다

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);          //요청이 정적,동적 컨텐츠인지 나타내는 플래그(uri를 파일 이름과 cgi인자 스트링으로 분석)를 설정    
    if (stat(filename, &sbuf) < 0) {                        //만약 이 파일이 디스크 상에 있지 않으면, 에러 메시지 출력
	clienterror(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
	return;
    }                                                    

    if (is_static) { /* Serve static content */                     //요청이 정적 컨텐츠라면
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {    //파일이 보통 파일인지와 읽기 권한을 가지고 있는지 검증한다
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't read the file");
	    return;
	}
	serve_static(fd, filename, sbuf.st_size);                       //에러가 없으면 정적 컨텐츠를 클라이언트에게 제공한다
    }
    else { /* Serve dynamic content */
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {    //요청이 동적 컨텐츠라면 파일이 실행 가능한지 검증하고
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't run the CGI program");
	    return;
	}
	serve_dynamic(fd, filename, cgiargs);                           //에러가 없으면 동적 컨텐츠를 클라이언트에 제공
    }
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs)     //uri를 파일 이름과 옵션으로 cgi 인자 스트링을 분석하는 함수
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {                          //요청이 정적 컨텐츠를 위한 것이라면
	strcpy(cgiargs, "");                                    //cgi인자 스트링을 지우고
	strcpy(filename, ".");                                  //uri를 ./index.html 같은 상대 리눅스 경로이름으로 변경한다
	strcat(filename, uri);                           
	if (uri[strlen(uri)-1] == '/')                          //만약 uri가 '/'로 끝난다면
	    strcat(filename, "home.html");                      //기본 파일 이름을 추가한다
	return 1;
    }
    else {                                                  //요청이 동적 컨텐츠를 위한 것이라면
	ptr = index(uri, '?');                                  //모든 cgi인자를 추출
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	}
	else 
	    strcpy(cgiargs, "");                         
	strcpy(filename, ".");                                  //나머지 uri부분을 상대 리눅스 파일로 변환한다
	strcat(filename, uri);                           
	return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    /* Send response headers to client */
    get_filetype(filename, filetype);                               //파일 이름의 접미어 부분을 검사해서 파일 타입을 결정
    sprintf(buf, "HTTP/1.0 200 OK\r\n");                            //클라이언트에 응답 줄과 응답 헤더를 보낸다
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));                               
    printf("Response headers:\n");                                  //서버에 출력
    printf("%s", buf);

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);                            //읽기 위해서 filename을 open하고 식별자를 얻어온다
    srcp = (*char)malloc(filesize);                                 //srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); 요청한 파일을 가상메모리 영역으로 매핑한다
    Rio_readn(srcfd, srcp, filesize);
    Close(srcfd);                                                   //매핑한 후에 식별자가 필요없어지면 파일을 닫는다
    Rio_writen(fd, srcp, filesize);                                 //파일을 클라이언트에 전송
    free(srcp);                                                     //Munmap(srcp, filesize); 매핑된 가상메모리 주소를 반환
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype)                   //서버가 처리할 수 있는 파일 타입을 이 함수를 통해 제공한다
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mpeg"))
    strcpy(filetype, "image/mpeg");
    else 
	strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) {                              /* Child */ //fork()를 통해 자식 프로세스를 생성하여 동적 컨텐츠를 제공한다
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1);             //자식은 QUERY_STRING 환경변수를 요청 URI의 CGI인자들로 초기화한다
	Dup2(fd, STDOUT_FILENO);                        //자식은 자식의 표준 출력을 연결 파일 식별자로 재지정하고 
	Execve(filename, emptylist, environ);           //CGI 프로그램을 로드하고 실행한다
    }
    Wait(NULL);                                     //부모는 자식이 종료되어 정리되는 것을 기다린다
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,                 //클라이언트에게 에러를 보고하는 함수
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
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
