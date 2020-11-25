#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

char *pbin_version_string = "0.0.1";

int main(int argc, char **argv) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		fprintf(stderr, "pbin error: Cannot open socket: %s\n",
			strerror(errno));
		return 1;
	}

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_addr.s_addr = inet_addr("0.0.0.0");
	addr.sin_family = AF_INET;
	addr.sin_port = htons(9999);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "pbin error: Cannot bind socket: %s\n",
			strerror(errno));
		close(sock);
		return 1;
	}

while (1) {
	if (listen(sock, 1) < 0) {
		fprintf(stderr, "pbin error: Cannot listen: %s\n",
			strerror(errno));
		close(sock);
		return 1;
	}

	struct sockaddr_in peer_addr;
	bzero(&addr, sizeof(peer_addr));
	socklen_t peer_addr_size = sizeof(peer_addr);
	int peer_sock = accept(sock, (struct sockaddr *)&peer_addr, &peer_addr_size);
	if (peer_sock < 0) {
		fprintf(stderr, "pbin error: Cannot accept connection: %s\n",
			strerror(errno));
		close(sock);
		return 1;
	}

	char filename[5], fullname[128];
	struct timeval tv0;
	gettimeofday(&tv0, NULL);
	srand((unsigned int)tv0.tv_usec);
	filename[0] = (rand()%10) + 48;
	filename[1] = (rand()%10) + 48;
	filename[2] = (rand()%10) + 48;
	filename[3] = (rand()%10) + 48;
	filename[4] = '\0';
	sprintf(fullname, "/srv/files/tmp/%s", filename);
	FILE *fw = fopen(fullname, "w");
	if (fw == NULL) {
		fprintf(stderr, "pbin error: Cannot open %s: %s\n", fullname,
			strerror(errno));
		close(sock);
		return 1;
	}

	ssize_t bytes_read;
	char buffer[4096001];
	memset(buffer, 0, 4096001);
	bytes_read = read(peer_sock, buffer, 4096000);
	if (bytes_read == -1) {
		fprintf(stderr, "pbin error: Cannot read(): %s\n",
			strerror(errno));
		close(sock);
		return 1;
	}
	else if (bytes_read == 0)
		return 0;

	fputs(buffer, fw);
	fclose(fw);
	
	// Change this to your server's address
	sprintf(buffer, "https://esselfe.ca/tmp/%s\n", filename);
	write(peer_sock, buffer, strlen(buffer));
	close(peer_sock);
}
	close(sock);
}
