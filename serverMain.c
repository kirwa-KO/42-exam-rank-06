#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>


typedef struct s_client
{
    int fd;
    int index;
    struct s_client *next;
} t_client;

int g_socketFd, g_index;
t_client *g_client = NULL;
fd_set current_set, write_set, read_set;

int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	// free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void fatal(){
    write(2, "Fatal error\n", 13);
    exit(1);
}

int getMaxFd(){
    int fd = g_socketFd;
    t_client *tmp = g_client;

    while (tmp){
        if (tmp->fd > fd)
            fd = tmp->fd;
        tmp = tmp->next;
    }
    return fd;
}

int getIdByfd(int fd){
    t_client *tmp = g_client;

    while (tmp){
        if (tmp->fd == fd)
            return tmp->index;
        tmp = tmp->next;
    }
    return 0;
}

void rmClientFromList(int fd){
    t_client *del, *head = g_client;

    if (g_client && g_client->fd == fd){
        del = g_client;
        g_client = g_client->next;
        free(del);
        return;
    }
    while (head && head->next && head->next->fd != fd)
        head = head->next;
    del = head->next;
    head->next = head->next->next;
    // printf("DBUG12\n");
    free(del);
    // printf("DBUG13\n");
    
}

void broadcastTheLeftMessage(int fd, int index){
    t_client *head = g_client;
    char str[50];

    bzero(str, 50); 
    sprintf(str, "server: client %d just left\n", index);
    while (head) {
        if (head->fd != fd &&  FD_ISSET(head->fd, &write_set)){
            if (send(head->fd, str, strlen(str), 0) < 0)
                fatal();
        }
        head = head->next;
    }
}

void broadcastTheJoinMessage(int fd, int index){
    t_client *head = g_client;
    char str[50];

    bzero(str, 50);
    sprintf(str, "server: client %d just arrived\n", index);
    while (head){
        if (head->fd != fd && FD_ISSET(head->fd, &write_set)){
            if (send(head->fd, str, strlen(str), 0) < 0)
                fatal();
        }
        head = head->next;
    }
    
}

void broadcastMessage(char *message, int fd, int id){
    int len = 0;
    t_client *head = g_client;
    char *joinableMessage = NULL;

    if (message)
        len = strlen(message);
    joinableMessage = malloc(len + 20);
    sprintf(joinableMessage, "client %d: %s", id, message);
    while (head){
        if (head->fd != fd && FD_ISSET(head->fd, &write_set)){
            if (send(head->fd, joinableMessage, strlen(joinableMessage), 0) < 0)
                fatal();
        }
        head = head->next;
    }
    free(joinableMessage);
}

void sendMessage(char *message, int fd){
    char *msg;
    int id = getIdByfd(fd);
    int check = extract_message(&message, &msg);
    //? leaks 
    while (check) {
        broadcastMessage(msg, fd, id);
        free(msg);
        check = extract_message(&message, &msg);
    }
    //? leaks 
    free(msg);
    free(message);
}

void addClient(){
    struct sockaddr_in cli; 
	int len = sizeof(cli);
    t_client *head = g_client;

    int clientFd = accept(g_socketFd, (struct sockaddr *)&cli, (socklen_t*)&len);
    if (clientFd < 0)
        fatal();
    if (g_client == NULL){
        g_client = malloc(sizeof(t_client));
        g_client->fd = clientFd;
        g_client->index = g_index;
        g_client->next = NULL;
    }
    else{
        while (head->next){
            head = head->next;
        }
        head->next = malloc(sizeof(t_client));
        head->next->fd = clientFd;
        head->next->index = g_index;
        head->next->next = NULL;
    }
    broadcastTheJoinMessage(clientFd, g_index);
    FD_SET(clientFd, &current_set);
    g_index++;
}

int main(int argc, char const *argv[]){
    struct sockaddr_in servaddr;
    if (argc != 2){
        write(2,"Wrong number of arguments\n", 27);
        exit(1);
    }
    g_socketFd = socket(AF_INET, SOCK_STREAM, 0); 
    if (g_socketFd == -1)
        fatal();
    bzero(&servaddr , sizeof(servaddr));
    servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); 
	if ((bind(g_socketFd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
        fatal();
    listen(g_socketFd, 10);
    FD_ZERO(&current_set);
    FD_SET(g_socketFd, &current_set);
    while (1){    
        write_set = current_set;
        read_set = current_set;
        int maxFd = getMaxFd();
        int selected = select(maxFd + 1, &read_set, &write_set, NULL, NULL);
        if (selected <= 0)
            continue;
        for (int fd = 0; fd < maxFd + 1; fd++){
            if (FD_ISSET(fd, &read_set)){
                if (fd == g_socketFd){
                    addClient();
                    break;
                }
                else{
                    int receved = 1000;
                    char str[1001];
                    char *message = NULL;
                    while (receved == 1000){
                        bzero(str, 1001);
                        receved = recv(fd,  str, 1000, 0);
                        if (receved <= 0)
                            break;
                        char *ptr = message;
                        message = str_join(message, str);
                        free(ptr);

                    }
                    if (receved <= 0){ //? remove user send notification clr 
                        int id = getIdByfd(fd);
                        broadcastTheLeftMessage(fd, id);
                        FD_CLR(fd, &current_set);
                        rmClientFromList(fd);
                        close(fd);
                    }
                    else
                        sendMessage(message, fd);
                }
            }
        }
    }
    return 0;
}