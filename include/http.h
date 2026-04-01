#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

struct NetInterface;

#define HTTP_MAX_URL      256u
#define HTTP_MAX_HOST     128u
#define HTTP_MAX_PATH     128u
#define HTTP_MAX_RESPONSE 8192u
#define HTTP_PORT         80u

typedef struct HttpResponse {
    int      status_code;
    char     content_type[64];
    uint32_t content_length;
    uint8_t  body[HTTP_MAX_RESPONSE];
    uint32_t body_len;
    int      complete;
} HttpResponse;

/* Parse one http:// URL into host, port, and path components. */
int http_parse_url(const char *url, char *host, uint16_t *port, char *path);

/* Perform one blocking HTTP/1.0 GET into an in-memory response buffer. */
int http_get(struct NetInterface *iface, const char *url,
             HttpResponse *resp, uint32_t timeout_ticks);

/* Perform one blocking HTTP/1.0 GET and write the body to one VFS path. */
int http_get_to_file(struct NetInterface *iface, const char *url,
                     const char *dest_path, uint32_t timeout_ticks);

/* Return the last human-readable HTTP client error string. */
const char *http_last_error(void);

#endif
