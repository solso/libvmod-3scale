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
  char* header;
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

int get_http_response_code(char* buffer, int buffer_len) {

  int first_space=0;
  int conti=0;
  char respcode[3];
  int i;

  for(i=0;i<buffer_len;i++) {
    if ((buffer[i]==32) && (first_space==0)) {
      first_space = 1;
    }
    else {
      if ((buffer[i]==32) && (first_space==1)) i=buffer_len;
      else {
        if (first_space==1) {
          respcode[conti]=buffer[i];
          conti++;  
        }
      }
    }  
  }
    
  return atoi(respcode);
  
}
        

char* send_get_request(struct request* req, int* http_response_code) {

  struct sockaddr_in *remote;
  int sock;
  int buffer_size = 16*1024;
  char* buffer = (char*)malloc(sizeof(char)*buffer_size);
  int tmpres;

  char* ip = get_ip(req->host);
  
  if (ip==NULL) {
    perror("libvmod_3scale: could not resolve the ip");
  }
  else {
    
    char* template;
    char* srequest;

    if ((req->header==NULL) || (strlen(req->header)==0)) {
      template = "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: Close\r\n\r\n";
      srequest = (char*)malloc(sizeof(char)*((int)strlen(template)+(int)strlen(req->path)+(int)strlen(req->host)-3));
      sprintf(srequest,template,req->path,req->host);

    }
    else {
      template = "GET %s HTTP/1.1\r\nHost: %s\r\n%s\r\nConnection: Close\r\n\r\n";
      srequest = (char*)malloc(sizeof(char)*((int)strlen(template)+(int)strlen(req->path)+(int)strlen(req->host)+(int)strlen(req->header)-5));
      sprintf(srequest,template,req->path,req->host,req->header);
    } 

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

        // FIXME: this will only work for long response pages > 16KB (buffer_size)
        recv(sock, buffer, buffer_size, 0);

        (*http_response_code) = get_http_response_code(buffer,buffer_size);

      }
      else {
        perror("libvmod_3scale: could not connect to socket");
      }

      free(remote);
      close(sock);

    }
    else {
      perror("libvmod_3scale: could not obtain socket");
    }

    free(srequest);
    free(ip);
  }

  return buffer;
  
}

void* send_get_request_thread(void* data) {

  struct request *req = (struct request *)data; 
  int http_response_code;

  char* buffer = send_get_request(req,&http_response_code);

  if (buffer!=NULL) free(buffer);
  if (req->host!=NULL) free(req->host);
  if (req->path!=NULL) free(req->path);
  if (req->header!=NULL) free(req->header);
  if (req!=NULL) free(req);

  pthread_exit(NULL);

}


int vmod_send_get_request(struct sess *sp, const char* host, const char* port, const char* path, const char* header) {

  int porti;
  if (port!=NULL && strcmp(port,"(null)")!=0) { 
    porti = atoi(port);
    if (porti<=0) porti=80;
  }

  struct request *req = (struct request*)malloc(sizeof(struct request));  
  req->host = strdup(host);
  req->path = strdup(path);
  req->header = strdup(header);
  req->port = porti;

  int http_response_code;
  char* http_body = send_get_request(req,&http_response_code);

  //printf("%d %s\n",http_response_code,http_body);
 
  if (req->host!=NULL) free(req->host);
  if (req->path!=NULL) free(req->path);
  if (req->header!=NULL) free(req->header);
  if (req!=NULL) free(req);
  if (http_body!=NULL) free(http_body);

  return http_response_code;

}


int vmod_send_get_request_threaded(struct sess *sp, const char* host, const char* port, const char* path, const char* header) {

  pthread_t tid;
 
  int porti = 80;
  if (port!=NULL && strcmp(port,"(null)")!=0) { 
    porti = atoi(port);
    if (porti<=0) porti=80;
  }

  struct request *req = (struct request*)malloc(sizeof(struct request));  
  req->host = strdup(host);
  req->path = strdup(path);
  if (header!=NULL) req->header = strdup(header);
  req->port = porti;

  pthread_create(&tid, NULL, send_get_request_thread,(void *)req);
  pthread_detach(tid);
  
  return 0;
}

/*
int main(int argc, char** argv) {

  vmod_send_get_request(NULL,"localhost","3001","/transactions/authrep.xml?provider_key=3scale-5fc9d398ac038e4e8f212cc1e8cf01d2&app_id=552740021&usage[hits]=1","X-bullshit: true;");

  vmod_send_get_request(NULL,"localhost","3001","/transactions/authrep.xml?provider_key=3scale-5fc9d398ac038e4e8f212cc1e8cf01d2&app_id=552740021&usage[hits]=1","");

}
*/
