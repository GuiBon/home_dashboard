#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <curl/curl.h>
#include "http.h"
#include "logging.h"

// HTTP response buffer structure
typedef struct {
    char *memory;
    size_t size;
    size_t capacity;
} MemoryStruct;

// Optimized callback function for writing HTTP response data
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    if (!contents || !userp) {
        return 0;
    }
    
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;
    
    // Check for integer overflow
    if (realsize == 0 || mem->size > SIZE_MAX - realsize - 1) {
        return 0;
    }
    
    size_t new_size = mem->size + realsize + 1;
    
    // Expand buffer if needed
    if (new_size > mem->capacity) {
        size_t new_capacity = mem->capacity;
        while (new_capacity < new_size) {
            new_capacity *= 2;
        }
        
        char *ptr = realloc(mem->memory, new_capacity);
        if (!ptr) {
            LOG_ERROR("HTTP: Memory allocation failed");
            return 0;
        }
        
        mem->memory = ptr;
        mem->capacity = new_capacity;
    }
    
    // Copy data and null-terminate
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = '\0';
    
    return realsize;
}

// Initialize memory structure with optimized buffer
static int init_memory_struct(MemoryStruct *mem) {
    if (!mem) {
        return -1;
    }
    
    mem->memory = malloc(HTTP_INITIAL_BUFFER_SIZE);
    if (!mem->memory) {
        return -1;
    }
    
    mem->size = 0;
    mem->capacity = HTTP_INITIAL_BUFFER_SIZE;
    mem->memory[0] = '\0';
    
    return 0;
}

// HTTP GET request with comprehensive error handling
char* http_get(const char* url) {
    if (!url || strlen(url) == 0) {
        LOG_ERROR("HTTP: Invalid URL provided");
        return NULL;
    }
    
    CURL *curl_handle;
    CURLcode res;
    MemoryStruct chunk;
    long response_code = 0;
    
    // Initialize memory structure
    if (init_memory_struct(&chunk) != 0) {
        LOG_ERROR("HTTP: Failed to initialize memory structure");
        return NULL;
    }
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_handle = curl_easy_init();
    
    if (!curl_handle) {
        LOG_ERROR("HTTP: Failed to initialize curl handle");
        free(chunk.memory);
        curl_global_cleanup();
        return NULL;
    }
    
    // Configure curl with optimized settings
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    
    // Timeout settings
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, HTTP_CONNECT_TIMEOUT);
    
    // Security and reliability settings
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, HTTP_MAX_REDIRECTS);
    curl_easy_setopt(curl_handle, CURLOPT_NOSIGNAL, 1L);  // Thread-safe
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Performance settings
    curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPINTVL, 30L);
    
    // Perform the request
    res = curl_easy_perform(curl_handle);
    
    if (res != CURLE_OK) {
        LOG_ERROR("HTTP: Request failed: %s", curl_easy_strerror(res));
        free(chunk.memory);
        chunk.memory = NULL;
    } else {
        // Check HTTP response code
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
        if (response_code >= 400) {
            LOG_ERROR("HTTP: Server returned error %ld", response_code);
            free(chunk.memory);
            chunk.memory = NULL;
        }
    }
    
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    
    return chunk.memory;
}