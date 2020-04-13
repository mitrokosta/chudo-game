#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const size_t BUFF_SIZE = 4096;

int main(int argc, char** argv) {
  if (argc < 3) {
    return -1;
  }
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(atoi(argv[2]));
  inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
  int success = connect(sockfd, (void*)&servaddr, sizeof(servaddr));
  if (success == -1) {
    puts("Connection failed");
    return -1;
  }
  char buff[BUFF_SIZE];
  char word[BUFF_SIZE];
  size_t word_len;
  ssize_t bytes_read = read(sockfd, buff, BUFF_SIZE);
  word_len = strlen(buff);
  while (bytes_read > 0) {
    strncpy(word, buff, BUFF_SIZE);
    unsigned attempts = atoi(buff + word_len + 1);
    printf("The word is: %s, attempts remaining: %d.\n", word, attempts);
    size_t total_len = 0;
    for (size_t i = 0; i < word_len; ++i) {
      if (word[i] != '*') {
        ++total_len;
      }
    }
    if (attempts > 0 && total_len < word_len) {
      printf("Please enter your character: ");
      char letter;
      do {
        scanf("%c", &letter);
      } while (!(letter <= 'z' && letter >= 'a'));

      write(sockfd, &letter, sizeof(letter));
      bytes_read = read(sockfd, buff, BUFF_SIZE);
    } else {
      break;
    }

  }

  shutdown(sockfd, SHUT_RDWR);
  close(sockfd);
  return 0;
}
