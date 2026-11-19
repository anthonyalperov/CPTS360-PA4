    #include "csapp.h"

    /* You won't lose style points for including this long line in your code */
    static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

    void handle_client(int connfd);

    int parse_uri(const char* uri, char* hostname, char* port, char* path);

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

    void handle_client(int connfd)
    {
        char buffer[MAXLINE];
        char method[MAXLINE];
        char uri[MAXLINE];
        char version[MAXLINE];
        char hostname[MAXLINE];
        char port[MAXLINE];
        char path[MAXLINE];
        char request[MAXLINE];
        char host_header[MAXLINE] = "";
        char header[MAXLINE];
        char other_headers[MAXBUF] = "";
        char response[MAXBUF];


        rio_t rio;

        Rio_readinitb(&rio, connfd);

        if (Rio_readlineb(&rio, buffer, MAXLINE) > 0)
        {
            printf("Received request: %s", buffer);
            
            if (sscanf(buffer, "%8191s %8191s %8191s", method, uri, version) != 3)
            {
                printf("Invalid HTTP request\n");
                return;
            }

            if (strcasecmp(method, "GET") != 0)
            {
                printf("Unsupported HTTP method: %s\n", method);
                return;
            }

            if (parse_uri(uri, hostname, port, path) != 0)
            {
                printf("Invalid HTTP URI\n");
                return;
            }

            while (Rio_readlineb(&rio, header, MAXLINE) > 0)
            {
                if (strcmp(header, "\r\n") == 0)
                {
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
                }
            }

            int serverfd = Open_clientfd(hostname, port);

            if (serverfd < 0)
            {
                printf("Failed to connect %s:%s\n", hostname, port);
                return;
            }

            printf("Connected to server: %s:%s\n", hostname, port);
            snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\n", path);

            Rio_writen(serverfd, request, strlen(request));

            if (host_header[0] != '\0')
            {
                Rio_writen(serverfd, host_header, strlen(host_header));
            }
            else
            {
                snprintf(request, sizeof(request), "Host: %s\r\n", hostname);
                Rio_writen(serverfd, request, strlen(request));
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

    int parse_uri(const char *uri, char *hostname, char *port, char *path)
    {
        if (strncmp(uri, "http://", 7) != 0)
        {
            return -1;
        }

        //To skip http://
        const char* host_start = uri + 7;

        const char* host_end = host_start;

        while (*host_end != 0 && *host_end != ':' && *host_end != '/')
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

            if (port_len == 0 || port_len > 5 ||
                (*port_end != '/' && *port_end != '\0'))
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
        else
        {   
            strcpy(path, "/");
        }  

        return 0;
    }