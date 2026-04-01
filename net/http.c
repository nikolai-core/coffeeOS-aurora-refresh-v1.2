#include <stdint.h>

#include "ascii_util.h"
#include "dns.h"
#include "http.h"
#include "netif.h"
#include "tcp.h"
#include "vfs.h"

static uint8_t http_recv_buf[HTTP_MAX_RESPONSE];
static const char *http_last_error_value = "ok";
static char http_host_buf[HTTP_MAX_HOST];
static char http_path_buf[HTTP_MAX_PATH];
static char http_req_buf[384];
static char http_host_header_buf[HTTP_MAX_HOST + 8u];

/* Store one short human-readable HTTP client error string. */
static void http_set_error(const char *msg) {
    http_last_error_value = msg;
}

/* Return non-zero when a bounded buffer starts with a fixed ASCII prefix. */
static int http_mem_starts_with(const uint8_t *buf, uint32_t len, const char *prefix) {
    uint32_t i = 0u;

    while (prefix[i] != '\0') {
        if (i >= len || buf[i] != (uint8_t)prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

/* Return the byte offset of the HTTP body once the header terminator is present. */
static uint32_t http_find_body_offset(const uint8_t *buf, uint32_t len) {
    uint32_t i;

    for (i = 0u; i + 3u < len; i++) {
        if (buf[i] == '\r' && buf[i + 1u] == '\n'
            && buf[i + 2u] == '\r' && buf[i + 3u] == '\n') {
            return i + 4u;
        }
    }
    for (i = 0u; i + 1u < len; i++) {
        if (buf[i] == '\n' && buf[i + 1u] == '\n') {
            return i + 2u;
        }
    }
    return 0u;
}

/* Parse one Content-Length header value from a buffered HTTP response header. */
static int http_find_content_length(const uint8_t *buf, uint32_t header_len, uint32_t *out_length) {
    uint32_t line = 0u;

    if (buf == (const uint8_t *)0 || out_length == (uint32_t *)0 || header_len == 0u) {
        return -1;
    }
    while (line < header_len) {
        if (http_mem_starts_with(buf + line, header_len - line, "Content-Length: ")) {
            uint32_t j = 16u;
            uint32_t value = 0u;

            while (line + j < header_len && buf[line + j] >= '0' && buf[line + j] <= '9') {
                value = value * 10u + (uint32_t)(buf[line + j] - '0');
                j++;
            }
            *out_length = value;
            return 0;
        }
        while (line + 1u < header_len
               && !(buf[line] == '\r' && buf[line + 1u] == '\n')
               && buf[line] != '\n') {
            line++;
        }
        if (line + 1u < header_len && buf[line] == '\r' && buf[line + 1u] == '\n') {
            line += 2u;
        } else if (line < header_len && buf[line] == '\n') {
            line += 1u;
        } else {
            break;
        }
    }
    return -1;
}

/* Parse one decimal TCP port number from URL authority text. */
static int http_parse_port(const char *text, uint16_t *out_port) {
    uint32_t value = 0u;

    if (text == (const char *)0 || out_port == (uint16_t *)0 || *text == '\0') {
        return -1;
    }
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return -1;
        }
        value = (value * 10u) + (uint32_t)(*text - '0');
        if (value == 0u || value > 65535u) {
            return -1;
        }
        text++;
    }
    *out_port = (uint16_t)value;
    return 0;
}

/* Parse one http:// URL into host, port, and path strings. */
int http_parse_url(const char *url, char *host, uint16_t *port, char *path) {
    uint32_t i = 0u;
    uint32_t h = 0u;
    uint32_t p = 0u;
    uint32_t port_start = 0u;

    if (url == (const char *)0 || host == (char *)0 || port == (uint16_t *)0 || path == (char *)0
        || !ascii_starts_with(url, "http://")) {
        http_set_error("invalid URL");
        return -1;
    }
    *port = HTTP_PORT;
    i = 7u;
    while (url[i] != '\0' && url[i] != '/' && url[i] != ':' && h + 1u < HTTP_MAX_HOST) {
        host[h++] = url[i++];
    }
    host[h] = '\0';
    if (host[0] == '\0') {
        http_set_error("missing host");
        return -1;
    }

    if (url[i] == ':') {
        i++;
        port_start = i;
        while (url[i] != '\0' && url[i] != '/') {
            i++;
        }
        {
            char port_text[6];
            uint32_t t = 0u;

            if (i == port_start) {
                http_set_error("missing port");
                return -1;
            }
            while (port_start < i && t + 1u < sizeof(port_text)) {
                port_text[t++] = url[port_start++];
            }
            if (port_start != i) {
                http_set_error("port too long");
                return -1;
            }
            port_text[t] = '\0';
            if (http_parse_port(port_text, port) != 0) {
                http_set_error("invalid port");
                return -1;
            }
        }
    }

    if (url[i] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    } else {
        while (url[i] != '\0' && p + 1u < HTTP_MAX_PATH) {
            path[p++] = url[i++];
        }
        path[p] = '\0';
    }
    return 0;
}

/* Perform one blocking HTTP/1.0 GET into an in-memory response buffer. */
int http_get(struct NetInterface *iface, const char *url,
             HttpResponse *resp, uint32_t timeout_ticks) {
    uint16_t port;
    uint32_t ip;
    int fd = -1;
    int sent;
    int got;
    uint32_t total = 0u;
    uint32_t body_off = 0u;
    uint32_t expected_body_len = 0u;
    int have_content_length = 0;
    uint32_t i;
    uint32_t attempt;

    http_set_error("ok");
    if (iface == (struct NetInterface *)0 || resp == (HttpResponse *)0
        || http_parse_url(url, http_host_buf, &port, http_path_buf) != 0) {
        if (iface == (struct NetInterface *)0 || resp == (HttpResponse *)0) {
            http_set_error("bad arguments");
        }
        return -1;
    }
    if (dns_resolve(http_host_buf, &ip) != 0) {
        http_set_error("DNS resolution failed");
        return -1;
    }

    for (i = 0u; i < sizeof(http_host_header_buf); i++) {
        http_host_header_buf[i] = '\0';
    }
    {
        uint32_t pos = 0u;

        while (http_host_buf[pos] != '\0' && pos + 1u < sizeof(http_host_header_buf)) {
            http_host_header_buf[pos] = http_host_buf[pos];
            pos++;
        }
        if (port != HTTP_PORT && pos + 1u < sizeof(http_host_header_buf)) {
            char port_digits[5];
            uint32_t count = 0u;
            uint32_t value = port;

            http_host_header_buf[pos++] = ':';
            while (value != 0u && count < sizeof(port_digits)) {
                port_digits[count++] = (char)('0' + (value % 10u));
                value /= 10u;
            }
            while (count != 0u && pos + 1u < sizeof(http_host_header_buf)) {
                http_host_header_buf[pos++] = port_digits[--count];
            }
            http_host_header_buf[pos] = '\0';
        }
    }

    for (i = 0u; i < sizeof(http_req_buf); i++) {
        http_req_buf[i] = '\0';
    }
    {
        uint32_t pos = 0u;
        const char *parts[] = {
            "GET ", http_path_buf, " HTTP/1.0\r\nHost: ", http_host_header_buf,
            "\r\nUser-Agent: coffeeOS/1.2\r\nConnection: close\r\n\r\n"
        };
        uint32_t part;
        uint32_t j;

        for (part = 0u; part < sizeof(parts) / sizeof(parts[0]); part++) {
            for (j = 0u; parts[part][j] != '\0' && pos + 1u < sizeof(http_req_buf); j++) {
                http_req_buf[pos++] = parts[part][j];
            }
        }
    }

    for (attempt = 0u; attempt < 2u; attempt++) {
        total = 0u;
        body_off = 0u;
        expected_body_len = 0u;
        have_content_length = 0;

        fd = tcp_connect(iface, ip, port, timeout_ticks);
        if (fd < 0) {
            http_set_error("TCP connect failed");
            return -1;
        }
        sent = tcp_send(fd, http_req_buf, (uint16_t)ascii_strlen(http_req_buf));
        if (sent < 0) {
            tcp_close(fd);
            http_set_error("HTTP request send failed");
            return -1;
        }

        while (total < HTTP_MAX_RESPONSE) {
            got = tcp_recv(fd, http_recv_buf + total, (uint16_t)(HTTP_MAX_RESPONSE - total), timeout_ticks);
            if (got <= 0) {
                break;
            }
            total += (uint32_t)got;
            if (body_off == 0u) {
                body_off = http_find_body_offset(http_recv_buf, total);
                if (body_off != 0u && http_find_content_length(http_recv_buf, body_off, &expected_body_len) == 0) {
                    have_content_length = 1;
                }
            }
            if (body_off != 0u && have_content_length && total >= body_off + expected_body_len) {
                break;
            }
        }
        tcp_close(fd);
        if (total != 0u) {
            break;
        }
    }
    if (total == 0u) {
        http_set_error("no HTTP response received");
        return -1;
    }

    resp->status_code = -1;
    resp->content_type[0] = '\0';
    resp->content_length = 0u;
    resp->body_len = 0u;
    resp->complete = 0;

    {
        body_off = http_find_body_offset(http_recv_buf, total);
        if (body_off == 0u) {
            http_set_error("invalid HTTP headers");
            return -1;
        }

        if (http_mem_starts_with(http_recv_buf, total, "HTTP/1.")) {
            resp->status_code = (http_recv_buf[9] - '0') * 100
                              + (http_recv_buf[10] - '0') * 10
                              + (http_recv_buf[11] - '0');
        } else {
            http_set_error("invalid HTTP status line");
            return -1;
        }

        {
            uint32_t line = 0u;

            while (line < body_off) {
                if (http_mem_starts_with(http_recv_buf + line, body_off - line, "Content-Type: ")) {
                    uint32_t j = 14u;
                    uint32_t k = 0u;

                    while (line + j < body_off && http_recv_buf[line + j] != '\r' && k + 1u < sizeof(resp->content_type)) {
                        resp->content_type[k++] = (char)http_recv_buf[line + j++];
                    }
                    resp->content_type[k] = '\0';
                } else if (http_mem_starts_with(http_recv_buf + line, body_off - line, "Content-Length: ")) {
                    uint32_t j = 16u;

                    while (line + j < body_off && http_recv_buf[line + j] >= '0' && http_recv_buf[line + j] <= '9') {
                        resp->content_length = resp->content_length * 10u + (uint32_t)(http_recv_buf[line + j] - '0');
                        j++;
                    }
                }
                while (line + 1u < body_off
                       && !(http_recv_buf[line] == '\r' && http_recv_buf[line + 1u] == '\n')
                       && http_recv_buf[line] != '\n') {
                    line++;
                }
                if (line + 1u < body_off && http_recv_buf[line] == '\r' && http_recv_buf[line + 1u] == '\n') {
                    line += 2u;
                } else if (line < body_off && http_recv_buf[line] == '\n') {
                    line += 1u;
                } else {
                    break;
                }
            }
        }

        resp->body_len = total - body_off;
        for (i = 0u; i < resp->body_len; i++) {
            resp->body[i] = http_recv_buf[body_off + i];
        }
        resp->complete = 1;
    }

    return 0;
}

/* Perform one blocking HTTP/1.0 GET and write the body to one VFS path. */
int http_get_to_file(struct NetInterface *iface, const char *url,
                     const char *dest_path, uint32_t timeout_ticks) {
    HttpResponse resp;

    if (http_get(iface, url, &resp, timeout_ticks) != 0) {
        return -1;
    }
    if (vfs_write_file(dest_path, resp.body, resp.body_len) != VFS_OK) {
        return -1;
    }
    return (int)resp.body_len;
}

/* Return the last human-readable HTTP client error string. */
const char *http_last_error(void) {
    return http_last_error_value;
}
