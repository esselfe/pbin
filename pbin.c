#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

char *pbin_version_string = "0.0.10";
// Base part of the URL returned to the user
char *site_url = "https://example.com/paste/";
// Local storage location of paste files
char *homedir = "/srv/files/paste";
char *log_filename = "/var/log/pbin.log";
char *filename;
int sock;
int peer_sock;
ssize_t bytes_read;
ssize_t bytes_read_total;
ssize_t bytes_read_total_prev;
unsigned int paste_size_max = 1000000;

void *DeleteStale(void *argp) {
	DIR *d;
	struct dirent *de;
	struct statx st;
	char fullname[1024];
	time_t t0;
	while (1) {
		d = opendir(homedir);
		if (d == NULL) {
			fprintf(stderr, "pbin error: Cannot open %s: %s\n",
				homedir, strerror(errno));
			exit(1);
		}

		while (1) {
			de = readdir(d);
			if (de == NULL)
				break;
			else if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;

			if (strlen(de->d_name) == 10 && isdigit(de->d_name[0]) && isdigit(de->d_name[1]) &&
					isdigit(de->d_name[2]) && isdigit(de->d_name[3]) && isdigit(de->d_name[4]) &&
					isdigit(de->d_name[5]) && strncmp(de->d_name + 6, ".txt", 4) == 0) {
				sprintf(fullname, "%s/%s", homedir, de->d_name);
				if (statx(0, fullname, 0, STATX_BTIME, &st) > -1) {
					t0 = time(NULL);
					if (st.stx_btime.tv_sec < t0 - 60*60*24*7)
						if (unlink(fullname) < 0) {
							fprintf(stderr,
							  "pbin error: Cannot unlink() %s: %s\n",
							  fullname, strerror(errno));
						}
				}
			}
		}
		
		closedir(d);
		sleep(10);
	}
	return NULL;
}

char *GenUniqueFilename(void) {
	char *name = malloc(12);
	struct timeval tv0;
	struct stat st;
	while (1) {
		gettimeofday(&tv0, NULL);
		srand((unsigned int)tv0.tv_usec);
		sprintf(name, "%06d.txt", rand()%1000000);
		if (stat(name, &st) == -1)
			break;
	}

	return name;
}

void *CheckEnd(void *argp) {
	time_t t0;
	time_t tprev = time(NULL);
	char buffer[1024];
	bytes_read_total_prev = 0;
	while (1) {
		t0 = time(NULL);
		if (bytes_read_total == bytes_read_total_prev) {
			if (t0 >= tprev + 2 || bytes_read_total > paste_size_max) {
				sprintf(buffer, "%s%s\n", site_url, filename);
				write(peer_sock, buffer, strlen(buffer));
				shutdown(peer_sock, 2);
				break;
			}
		}
		else {
			tprev = t0;
			bytes_read_total_prev = bytes_read_total;
		}

		sleep(1);
	}

	return NULL;
}

int main(int argc, char **argv) {
	if (chdir(homedir) < 0) {
		fprintf(stderr, "pbin error: Cannot change directory: %s\n",
			strerror(errno));
		return 1;
	}

	pthread_t thr;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&thr, &attr, DeleteStale, NULL);
	pthread_detach(thr);
	pthread_attr_destroy(&attr);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		fprintf(stderr, "pbin error: Cannot open socket: %s\n",
			strerror(errno));
		return 1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
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
	memset(&peer_addr, 0, sizeof(peer_addr));
	socklen_t peer_addr_size = sizeof(peer_addr);
	peer_sock = accept(sock, (struct sockaddr *)&peer_addr, &peer_addr_size);
	if (peer_sock < 0) {
		fprintf(stderr, "pbin error: Cannot accept connection: %s\n",
			strerror(errno));
		continue;
	}

	filename = GenUniqueFilename();

	FILE *fw = fopen(filename, "w");
	if (fw == NULL) {
		fprintf(stderr, "pbin error: Cannot open %s: %s\n", filename,
			strerror(errno));
		close(sock);
		return 1;
	}

	pthread_t thr2;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&thr2, &attr, CheckEnd, NULL);
	pthread_detach(thr2);
	pthread_attr_destroy(&attr);

	char buffer[4096001];
	while (1) {
		memset(buffer, 0, 4096001);
		errno = 0;
		bytes_read = read(peer_sock, buffer, 1096);
		if (bytes_read == -1) {
			if (errno)
				fprintf(stderr, "pbin error: Cannot read(): %s\n",
					strerror(errno));
			close(sock);
			break;
		}
		else if (bytes_read == 0)
			break;
		else
			bytes_read_total += bytes_read;

		fputs(buffer, fw);
	}
	fclose(fw);

	fw = fopen(log_filename, "a+");
	if (fw == NULL) {
		fprintf(stderr, "pbin error: Cannot open %s: %s\n", log_filename,
			strerror(errno));
	}
	else {
		struct stat st;
		stat(filename, &st);
		time_t t0 = time(NULL);
		struct tm *tm0 = malloc(sizeof(struct tm));
		localtime_r(&t0, tm0);
		char buffer2[1024];
		memset(buffer2, 0, 1024);
		sprintf(buffer2, "%02d%02d%02d-%02d%02d%02d %s %ld %s\n",
			tm0->tm_year+1900-2000, tm0->tm_mon+1, tm0->tm_mday, tm0->tm_hour,
			tm0->tm_min, tm0->tm_sec, filename, st.st_size,
			inet_ntoa(peer_addr.sin_addr));
		free(tm0);
		printf("%s", buffer2);
		fputs(buffer2, fw);
		fclose(fw);
	}

	free(filename);
}
}

