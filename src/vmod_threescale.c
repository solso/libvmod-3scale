#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>


#include "vrt.h"
#include "bin/varnishd/cache.h"
#include "vcc_if.h"

#define HTTP_GET 1
#define HTTP_POST 2

char *url_encode(const char *str);

struct request {
    char* host;
    char* path;
    char* header;
    char* body;
    int port;
    int http_verb;
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
        ipstr = NULL;
        return NULL;
    }

    struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
    addr = &(ipv4->sin_addr);

    if (inet_ntop(res->ai_family, addr, ipstr, iplen+1) == NULL) {
        free(ipstr);
        ipstr = NULL;
        freeaddrinfo(res);
        res = NULL;
        return NULL;
    }
    else {
        freeaddrinfo(res);
        res = NULL;
        return ipstr;
    }


}

int get_http_response_code(const char* buffer, int buffer_len) {

    int first_space=0;
    int conti=0;
    char respcode[3];
    int i;

    for(i=0;i<buffer_len;i++) {
        if ((buffer[i]==32) && (first_space==0)) {
            first_space = 1;
        }
        else {
            if ((buffer[i]==32) && (first_space==1)) {
                respcode[conti]='\0';
                i=buffer_len;
            }
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

char* get_string_between_delimiters(const char* string, const char* left, const char* right) {

    const char* beginning = strstr(string, left);
    if (beginning == NULL) return NULL;

    const char* end = strstr(string, right);
    if(end == NULL) return NULL;

    beginning += strlen(left);
    ptrdiff_t len = end - beginning;

    if (len<=0) return NULL;
    char* out = malloc(len + 1);
    strncpy(out, beginning, len);

    (out)[len] = 0;
    return out;
}


char* send_request(struct request* req, int* http_response_code) {

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

        if (req->http_verb==HTTP_POST) {

            int body_len = strlen(req->body);
            char tmp[128];
            sprintf(tmp,"%d",body_len);
            int body_len_len = strlen(tmp);

            if ((req->header==NULL) || (strlen(req->header)==0)) {
                template = "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\nConnection: Close\r\n\r\n%s";
                srequest = (char*)malloc(sizeof(char)*((int)strlen(template)+(int)strlen(req->path)+(int)strlen(req->host)+body_len+body_len_len-7));
                sprintf(srequest,template,req->path,req->host,body_len,req->body);
            }
            else {
                template = "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n%s\r\nConnection: Close\r\n\r\n%s";
                srequest = (char*)malloc(sizeof(char)*((int)strlen(template)+(int)strlen(req->path)+(int)strlen(req->host)+(int)strlen(req->header)+body_len+body_len_len-9));
                sprintf(srequest,template,req->path,req->host,body_len,req->header,req->body);
            }

        }
        else {
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

                // FIXME: this will fail for long response pages > 16KB (buffer_size)
                recv(sock, buffer, buffer_size, 0);

                (*http_response_code) = get_http_response_code(buffer,buffer_size);

            }
            else {
                perror("libvmod_3scale: could not connect to socket");
            }

            free(remote);
            remote = NULL;
            close(sock);

        }
        else {
            perror("libvmod_3scale: could not obtain socket");
        }

        free(srequest);
        srequest = NULL;
        free(ip);
        ip = NULL;
    }

    return buffer;

}

void* send_request_thread(void* data) {

    struct request *req = (struct request *)data;
    int http_response_code;

    char* buffer = send_request(req,&http_response_code);

    free(buffer);
    buffer=NULL;
    free(req->host);
    req->host=NULL;
    free(req->path);
    req->path=NULL;
    free(req->header);
    req->header=NULL;
    free(req->body);
    req->body=NULL;
    free(req);
    req=NULL;

    pthread_exit(NULL);

}

// ****************************************************************************
// credits for to_hex and url_encode: http://www.geekhideout.com/urlcode.shtml

/* Converts an integer value to its hex character*/
char to_hex(char code) {
    static char hex[] = "0123456789abcdef";
    return hex[code & 15];
}

/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_encode(const char *str) {
    const char *pstr = str;
    char *buf = (char*)malloc(strlen(str) * 3 + 1);
    char *pbuf = buf;
    while (*pstr) {
        if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
            *pbuf++ = *pstr;
        else if (*pstr == ' ')
            *pbuf++ = '+';
        else
            *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
        pstr++;
    }
    *pbuf = '\0';
    return buf;
}


// ****************************************************************************

const char *vmod_url_encode(struct sess *sp, const char* string) {
    return url_encode(string);
}

int vmod_response_http_code(struct sess *sp, const char* response_body) {

    if (response_body==NULL) return -1;
    int len = strlen(response_body);
    if (len>0) {
        return get_http_response_code(response_body,len);
    }
    else return -1;

}

const char* vmod_response_key(struct sess *sp, const char* response_body) {

    if (response_body==NULL) return NULL;
    int len = strlen(response_body);
    if (len>0) {
        return get_string_between_delimiters(response_body,"<key>","</key>");
    }
    else return NULL;

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
    req->http_verb = HTTP_GET;

    int http_response_code;
    char* http_body = send_request(req,&http_response_code);

    free(req->host);
    req->host=NULL;
    free(req->path);
    req->path=NULL;
    free(req->header);
    req->header=NULL;
    free(req);
    req=NULL;
    free(http_body);
    http_body=NULL;

    return http_response_code;

}

const char* vmod_send_get_request_body(struct sess *sp, const char* host, const char* port, const char* path, const char* header) {

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
    req->http_verb = HTTP_GET;

    int http_response_code;
    char* http_body = send_request(req, &http_response_code);

    free(req->host);
    req->host=NULL;
    free(req->path);
    req->path=NULL;
    free(req->header);
    req->header=NULL;
    free(req);
    req=NULL;

    return http_body;

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
    req->body = NULL;
    if (header!=NULL) req->header = strdup(header);
    req->port = porti;
    req->http_verb = HTTP_GET;

    pthread_create(&tid, NULL, send_request_thread,(void *)req);
    pthread_detach(tid);

    return 0;
}

int vmod_send_post_request_threaded(struct sess *sp, const char* host, const char* port, const char* path, const char* header, const char* body) {

    pthread_t tid;

    int porti = 80;
    if (port!=NULL && strcmp(port,"(null)")!=0) {
        porti = atoi(port);
        if (porti<=0) porti=80;
    }

    struct request *req = (struct request*)malloc(sizeof(struct request));
    req->host = strdup(host);
    req->path = strdup(path);
    req->body = strdup(body);
    if (header!=NULL) req->header = strdup(header);
    req->port = porti;
    req->http_verb = HTTP_POST;

    pthread_create(&tid, NULL, send_request_thread,(void *)req);
    pthread_detach(tid);

    return 0;
}

