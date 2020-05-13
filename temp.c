    char *tmp_header = malloc(max_sz);
    char *tmp_header_ptr = tmp_header;

    int buf_sz = 0;
    do
    {
        int offset = read(con->sck, buf, max_sz);
        strncpy(tmp_header, buf, max_sz - 1);
        buf_sz += offset;
        tmp_header += offset;
        max_sz -= offset;
    } while (max_sz > 0 && strstr(buf, "\r\n\r\n") == NULL);

    *tmp_header = '\0';
    header = tmp_header_ptr;

    int header_flag = 0;
    
    while (*(tmp_header_ptr + 3))
    {
        if (*tmp_header_ptr == '\r' && *(tmp_header_ptr + 1) == '\n' && *(tmp_header_ptr + 2) == '\r' && *(tmp_header_ptr + 3) == '\n')
        {
            *tmp_header_ptr = '\0';
            header_flag = 1;
            break; /* header is now the header */
        }
        tmp_header_ptr++;
    }

    if (header_flag == 0) /* malformed */
        return 0;

    puts("");
    puts("--------HEADER----------");
    puts("");
    puts(header);

    char *content_len = strstr(header, "content-length: ");
    if (content_len != NULL)
    {
        content_len = strtok(content_len, "\n");
        content_len = strtok(content_len, "content-length: ");
        body_sz = strtol(content_len, NULL, 10);
    }

    else
        body_sz = strtol(strtok((tmp_header_ptr + 3), "\n"), NULL, 16); /* the size of the body to fetch */

    /*
    char *tmp;
    while ((tmp = strtok(header, "\n")))
    {
        puts(tmp);
        header += strlen(tmp) + 1;
    }
*/
    puts("");
    puts("--------SIZE OF BODY----------");
    puts("");

    printf("0x%lx\n", body_sz);
    printf("%ld\n", body_sz);
