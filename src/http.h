#ifndef HTTP_H
#define HTTP_H

// Constants
#define HTTP_TIMEOUT_SECONDS 10L
#define HTTP_INITIAL_BUFFER_SIZE 1024
#define HTTP_USER_AGENT "dashboard/1.0"
#define HTTP_MAX_REDIRECTS 10L
#define HTTP_CONNECT_TIMEOUT 5L

// HTTP utility function
char* http_get(const char* url);

#endif // HTTP_H