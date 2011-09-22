#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"
#include "vcc_if.h"

struct request {
  char* host;
  char* path;
  int port;
};


int init_function(struct vmod_priv *priv, const struct VCL_conf *conf) {
  return (0);
}


char* get_ip(const char *host) {

  struct addrinfo hints, *res, *p;
  int status;
  int iplen = 15;
  void *addr;
  char *ipstr = (char *)malloc(iplen+1);
  memset(ipstr, 0, iplen+1);
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  
  if( (status = getaddrinfo(host, NULL, &hints, &res) ) != 0) {
    free(ipstr);
    return NULL;
  }
    
  struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
  addr = &(ipv4->sin_addr);

  if (inet_ntop(res->ai_family, addr, ipstr, iplen+1) == NULL) {
    free(ipstr);
    freeaddrinfo(res);  
    return NULL;
  }
  else {  
    freeaddrinfo(res);
    return ipstr;    
  }


}



void* send_get_request(void* data) {

  struct request *req = (struct request *)data; 
  struct sockaddr_in *remote;
  int sock;
  int buffer_size = 32;
  char* buffer = (char*)malloc(sizeof(char)*buffer_size);
  int tmpres;

  if (req->host==NULL) {
    exit(-1);
  }

  char* ip = get_ip(req->host);
  
  if (ip==NULL) {
    perror("livmod_3scale: could not resolve the ip");
  }
  else {
    
    char* template = "GET %s&varnish HTTP/1.1\r\nHost: %s\r\nConnection: Close\r\n\r\n";
    char* srequest = (char*)malloc(sizeof(char)*((int)strlen(template)+(int)strlen(req->path)+(int)strlen(req->host)-3));

    sprintf(srequest,template,req->path,req->host);

    if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) >= 0) {

      remote = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in *));
      remote->sin_family = AF_INET;
      
      inet_pton(AF_INET, ip, (void *)(&(remote->sin_addr.s_addr)));
      remote->sin_port = htons(req->port);

      if(connect(sock, (struct sockaddr *)remote, sizeof(struct sockaddr)) >= 0) {
        int sent = 0;
        while(sent < (int)strlen(srequest)) {
          tmpres = send(sock, srequest+sent, (int)strlen(srequest)-sent, 0);
          sent += tmpres;
        }

        recv(sock,buffer,buffer_size,0);

      }
      else {
        perror("livmod_3scale: could not connect to socket");
      }

      free(remote);
      close(sock);


    }
    else {
      perror("livmod_3scale: could not obtain socket");
    }

    free(srequest);
    free(ip);
  
  }

  free(buffer);
  free(req->path);
  free(req->host);
  free(req);

  pthread_exit(NULL);

 
}



int vmod_request_no_response(struct sess *sp, const char* host, const char* port, const char* url) {

  pthread_t tid;
 
  int porti = 80;
  if (port!=NULL && strcmp(port,"(null)")!=0) { 
    porti = atoi(port);
    if (porti<=0) porti=80;
  }

  struct request *req = (struct request*)malloc(sizeof(struct request));  
  req->host = strdup(host);
  req->path = strdup(url);
  req->port = porti;

  pthread_create(&tid, NULL, send_get_request,(void *)req);
  pthread_detach(tid);
  
}


