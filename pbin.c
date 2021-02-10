#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

char *pbin_version_string = "0.0.7";
char *homedir = "/srv/files/tmp";
char *log_filename = "/var/log/pbin.log";
char *filename;
int break_read, reading, peer_sock, sock;
struct sockaddr_in peer_addr;
FILE *f_write;
ssize_t bytes_read, bytes_read_total, bytes_read_total_prev;

void *DeleteStale(void *argp) {
	DIR *d;
	struct dirent *de;
	struct statx st;
	char *dirname = "/srv/files/tmp";
	char fullname[1024];
	char buffer[1028];
	time_t t0;
	while (1) {
		d = opendir(dirname);
		if (d == NULL) {
			fprintf(stderr, "pbin error: Cannot open %s: %s\n", dirname,
				strerror(errno));
			sleep(60);
			continue;
		}

		while (1) {
			de = readdir(d);
			if (de == NULL)
				break;
			else if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;

			if (strlen(de->d_name) == 4 && isdigit(de->d_name[0]) && isdigit(de->d_name[1]) &&
					isdigit(de->d_name[2]) && isdigit(de->d_name[3])) {
				sprintf(fullname, "%s/%s", dirname, de->d_name);
				if (statx(0, fullname, 0, STATX_BTIME, &st) > -1) {
					t0 = time(NULL);
					if (st.stx_btime.tv_sec < t0 - 60*60*24) {
						sprintf(buffer, "rm %s", fullname);
						system(buffer);
					}
				}
			}
		}
		
		closedir(d);
		sleep(15);
	}
	return NULL;
}

void *ReadData(void *argp) {
	f_write = fopen(filename, "w");
    if (f_write == NULL) {
        fprintf(stderr, "pbin error: Cannot open %s: %s\n", filename,
            strerror(errno));
        close(sock);
        return NULL;
    }
    char tbuffer[4096001];
    while (1) {
        memset(tbuffer, 0, 4096001);
        errno = 0;
        printf("reading...\n");
        bytes_read = read(peer_sock, tbuffer, 4096);
        printf("bytes_read: %ld\n", bytes_read);
        if (bytes_read == -1) {
            if (errno)
                fprintf(stderr, "pbin error: Cannot read(): %s\n",
                    strerror(errno));
            break;;
        }
  		else if (bytes_read == 0)
            break;
		else
			bytes_read_total += bytes_read;

        fputs(tbuffer, f_write);
		fflush(f_write);
		sync();
    }

	return NULL;
}

char *GenUniqueFilename(void) {
	char *name = malloc(6);
	struct timeval tv0;
	struct stat st;
	while (1) {
		gettimeofday(&tv0, NULL);
		srand((unsigned int)tv0.tv_usec);
		sprintf(name, "%04d", rand()%10000);
		if (stat(name, &st) == -1)
			break;
	}

	return name;
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

	bzero(&addr, sizeof(peer_addr));
	socklen_t peer_addr_size = sizeof(peer_addr);
	peer_sock = accept(sock, (struct sockaddr *)&peer_addr, &peer_addr_size);
	if (peer_sock < 0) {
		fprintf(stderr, "pbin error: Cannot accept connection: %s\n",
			strerror(errno));
		close(sock);
		return 1;
	}

	filename = GenUniqueFilename();
/*	f_write = fopen(filename, "w");
	if (f_write == NULL) {
		fprintf(stderr, "pbin error: Cannot open %s: %s\n", filename,
			strerror(errno));
		close(sock);
		return 1;
	}
*/
	pthread_t thr2;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&thr2, &attr, ReadData, NULL);
	pthread_detach(thr2);
	pthread_attr_destroy(&attr);

	time_t t1, tprev = time(NULL);
	while (1) {
		t1 = time(NULL);
		if (bytes_read_total == bytes_read_total_prev) {
			if (t1 >= tprev + 10) {
				printf("thread canceled\n");
				fclose(f_write);
				pthread_cancel(thr2);
				bytes_read_total = 0;
				bytes_read_total_prev = 0;
				break;
			}
		}
		else {
			tprev = t1;
			bytes_read_total_prev = bytes_read_total;
		}
		usleep(250000);
	}

	printf("loop done\n");

	char buffer[1024];
	sprintf(buffer, "https://esselfe.ca/tmp/%s\n", filename);
	write(peer_sock, buffer, strlen(buffer));
	close(peer_sock);

	f_write = fopen(log_filename, "a+");
	if (f_write == NULL) {
		fprintf(stderr, "pbin error: Cannot open %s: %s\n", log_filename,
			strerror(errno));
	}
	else {
		struct stat st;
		stat(filename, &st);
		time_t t0 = time(NULL);
		struct tm *tm0 = localtime(&t0);
		char buffer[1024];
		memset(buffer, 0, 1024);
		sprintf(buffer, "%02d%02d%02d-%02d%02d%02d %s %ld %s\n",
			tm0->tm_year+1900-2000, tm0->tm_mon+1, tm0->tm_mday, tm0->tm_hour,
			tm0->tm_min, tm0->tm_sec, filename, st.st_size, inet_ntoa(peer_addr.sin_addr));
		fputs(buffer, f_write);
		fclose(f_write);
	}

	free(filename);
}
	close(sock);
}

