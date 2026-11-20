    #include "csapp.h"

    /* You won't lose style points for including this long line in your code */
    static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

    // Sends an HTTP error response to the client.
    void send_error(int connfd, const char *status, const char *message);

    // Validates and forwards HTTP GET requests and relays server responses.
    void handle_client(int connfd);

    // Extracts the hostname, port, and path from an HTTP URI.
    int parse_uri(const char* uri, char* hostname, char* port, char* path);

    // Starts the proxy server and handles client connections sequentially.
    int main(int argc, char **argv)
    {   
        int listenfd, connfd;
        socklen_t clientlen;
        struct sockaddr_storage clientaddr;

        if (argc != 2) 
        {
            fprintf(stderr, "Usage: %s <port>\n", argv[0]);
            return 1;
        }

        listenfd = Open_listenfd(argv[1]);

        while (1) 
        {
            clientlen = sizeof(clientaddr);

            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

            printf("Client connected\n");

            handle_client(connfd);


            Close(connfd);
        }

        return 0;
    }

    // Sends an HTTP error response to the client.
    void send_error(int connfd, const char *status, const char *message)
    {
        char response[512];

        int length = snprintf(response, sizeof(response), "HTTP/1.0 %s\r\n" "Content-Type: text/plain\r\n" "Connection: close\r\n" "\r\n" "%s\n", status, message);

        if (length > 0 && (size_t)length < sizeof(response))
        {
            Rio_writen(connfd, response, length);
        }
    }

    // Validates and forwards HTTP GET requests and relays server responses.
    void handle_client(int connfd)
    {
        char buffer[MAXLINE];
        char method[MAXLINE];
        char uri[MAXLINE];
        char version[MAXLINE];
        char extra;
        char hostname[MAXLINE];
        char port[MAXLINE];
        char path[MAXLINE];
        char request[MAXLINE];
        char host_header[MAXLINE] = "";
        char header[MAXLINE];
        char other_headers[MAXBUF] = "";
        char response[MAXBUF];
        ssize_t header_bytes;
        int headers_complete = 0;
        int headers_valid = 1;

        rio_t rio;

        Rio_readinitb(&rio, connfd);

        if (Rio_readlineb(&rio, buffer, MAXLINE) > 0)
        {
            printf("Received request: %s", buffer);
            
            if (strchr(buffer, '\n') == NULL || sscanf(buffer, "%8191s %8191s %8191s %c", method, uri, version, &extra) != 3 || (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0))
            {
                printf("Invalid HTTP request\n");
                send_error(connfd, "400 Bad Request", "Invalid HTTP request");
                return;
            }

           if (strcasecmp(method, "GET") != 0)
            {
                printf("Unsupported HTTP method: %s\n", method);
                send_error(connfd, "501 Not Implemented", "501 Not Implemented");
                return;
            }

            if (parse_uri(uri, hostname, port, path) != 0)
            {
                printf("Invalid HTTP URI\n");
                send_error(connfd, "400 Bad Request", "Invalid HTTP URI");
                return;
            }

            while ((header_bytes = Rio_readlineb(&rio, header, MAXLINE)) > 0)
            {
                if (strcmp(header, "\r\n") == 0)
                {
                    headers_complete = 1;
                    break;
                }

                if (header_bytes == MAXLINE - 1 && header[header_bytes - 1] != '\n')
                {
                    headers_valid = 0;
                    break;
                }

                if (strncasecmp(header, "Host:", 5) == 0)
                {
                    snprintf(host_header, sizeof(host_header), "%s", header);
                }

                else if (strncasecmp(header, "Connection:", 11) == 0 || strncasecmp(header, "Proxy-Connection:", 17) == 0 || strncasecmp(header, "User-Agent:", 11) == 0)
                {
                    continue;
                }
                else
                {
                    size_t used = strlen(other_headers);
                    size_t remaining = sizeof(other_headers) - used;

                    if (strlen(header) < remaining)
                    {
                        strcat(other_headers, header);
                    }
                    else
                    {
                        headers_valid = 0;
                        break;
                    }
                }
            }

            if (!headers_complete || !headers_valid)
            {
                printf("Incomplete or oversized HTTP headers\n");
                send_error(connfd, "400 Bad Request", "Incomplete or oversized HTTP headers");
                return;
            }

            int serverfd = open_clientfd(hostname, port);

            if (serverfd < 0)
            {
                 printf("Failed to connect %s:%s\n", hostname, port);
                send_error(connfd, "502 Bad Gateway", "502 Bad Gateway");
                return;
            }

            printf("Connected to server: %s:%s\n", hostname, port);
            int request_length = snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\n", path);

            if (request_length < 0 || (size_t)request_length >= sizeof(request))
            {
                printf("HTTP request too long\n");
                Close(serverfd);
                return;
            }

            Rio_writen(serverfd, request, request_length);

            if (host_header[0] != '\0')
            {
                Rio_writen(serverfd, host_header, strlen(host_header));
            }
            else
            {
                int host_length = snprintf(request, sizeof(request), "Host: %s\r\n", hostname);

                if (host_length < 0 || (size_t)host_length >= sizeof(request))
                {
                    printf("Host header too long\n");
                    Close(serverfd);
                    return;
                }

                Rio_writen(serverfd, request, host_length);
            }

            Rio_writen(serverfd, (void *)user_agent_hdr, strlen(user_agent_hdr));
            Rio_writen(serverfd, "Connection: close\r\n", strlen("Connection: close\r\n"));
            Rio_writen(serverfd, "Proxy-Connection: close\r\n", strlen("Proxy-Connection: close\r\n"));

            if (other_headers[0] != 0)
            {
                Rio_writen(serverfd, other_headers, strlen(other_headers));
            }

            Rio_writen(serverfd, "\r\n", 2);

            rio_t server_rio;
            ssize_t bytes_read;

            Rio_readinitb(&server_rio, serverfd);

            while ((bytes_read = Rio_readnb(&server_rio, response, MAXBUF)) > 0)
            {
                Rio_writen(connfd, response, bytes_read);
            }

            Close(serverfd);

            printf("Hostname: %s\n", hostname);
            printf("Port: %s\n", port);
            printf("Path: %s\n", path);
            printf("Method: %s\n", method);
            printf("URI: %s\n", uri);
            printf("Version: %s\n", version);
        }
    }

    // Extracts the hostname, port, and path from an HTTP URI.
    int parse_uri(const char *uri, char *hostname, char *port, char *path)
    {
        if (strncmp(uri, "http://", 7) != 0)
        {
            return -1;
        }

        //To skip http://
        const char* host_start = uri + 7;

        const char* host_end = host_start;

        while (*host_end != 0 && *host_end != ':' && *host_end != '/' && *host_end != '?')
        {
            host_end++;
        }

        size_t host_len = host_end - host_start;

        if (host_len == 0 || host_len >= MAXLINE)
        {
            return -1;
        }

        memcpy(hostname, host_start, host_len);
        hostname[host_len] = '\0';

        strcpy(port, "80");

        if (*host_end == ':')
        {
            const char *port_start = host_end + 1;
            const char *port_end = port_start;

            while (*port_end >= '0' && *port_end <= '9')
            {
                port_end++;
            }

            size_t port_len = port_end - port_start;

            if (port_len == 0 || port_len > 5 || (*port_end != '/' && *port_end != '?' &&*port_end != '\0'))
            {
                return -1;
            }

            memcpy(port, port_start, port_len);
            port[port_len] = '\0';

            int port_number = atoi(port);

            if (port_number < 1 || port_number > 65535)
            {
                return -1;
            }

            host_end = port_end;
        }

        if (*host_end == '/')
        {
            if (strlen(host_end) >= MAXLINE)
            {
                return -1;
            }

            strcpy(path, host_end);
        }
        else if (*host_end == '?')
        {
            if (strlen(host_end) >= MAXLINE - 1)
            {
                return -1;
            }

            snprintf(path, MAXLINE, "/%s", host_end);
        }
        else
        {
            strcpy(path, "/");
        }

        return 0;
    }