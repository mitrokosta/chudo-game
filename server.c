#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/stat.h>

volatile sig_atomic_t alive = 1;

static const size_t BUFF_SIZE = 4096;
static const char* USAGE_STR = "Usage: <executable name> <port number> <maximum attempt number> <path to names file>";
static const char* NO_WORDS_STR = "Error: file does not exist.";

int PrepareWords(const char* words_path, void** words, size_t* words_len) {
  struct stat buff;
  int fd = open(words_path, O_RDONLY);
  if (fd == -1) {
    return -1;
  }
  fstat(fd, &buff);
  *words_len = buff.st_size;
  *words = calloc(*words_len, 1);
  char read_buff[BUFF_SIZE];
  size_t total_read = 0;
  ssize_t bytes_read = 0;
  do {
    bytes_read = read(fd, read_buff, BUFF_SIZE);
    if (bytes_read > 0) {
      memcpy(*words + total_read, read_buff, bytes_read);
      total_read += bytes_read;
    }
  } while (bytes_read > 0);

  close(fd);
  return 0;
}

unsigned CountWords(const char* words, size_t words_len) {
  char* cpy = calloc(words_len, 1);
  memcpy(cpy, words, words_len);
  char* pch = strtok(cpy, "\n");
  unsigned counter = 0;
  while (pch != NULL) {
    ++counter;
    pch = strtok(NULL, "\n");
  }
  free(cpy);
  return counter;
}

void FreeWords(void* words) {
  free(words);
}

void ChooseRandomWord(const char* words, size_t words_len, unsigned words_count, char* buff) {
  char* cpy = calloc(words_len, 1);
  memcpy(cpy, words, words_len);
  unsigned idx = rand() % words_count;
  char* pch = strtok(cpy, "\n");
  unsigned counter = 0;
  while (counter < idx) {
    pch = strtok(NULL, "\n");
    ++counter;
  }
  strcpy(buff, pch);
  free(cpy);
}

typedef struct {
  int fd;
  char* word;
  int* known;
  size_t word_len;
  unsigned attempts_num;
} Player;

void SetNonBlock(int fd) {
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

void SigAct(int signum) {
  alive = 0;
}

void RegisterSAHandler() {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = SigAct;
  sigaction(SIGTERM, &act, NULL);
}

int PrepareSocket(char* port_str) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  SetNonBlock(sockfd);
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(atoi(port_str));
  servaddr.sin_addr.s_addr = INADDR_ANY;
  bind(sockfd, (void*)&servaddr, sizeof(servaddr));
  listen(sockfd, SOMAXCONN);
  return sockfd;
}

int PrepareEpoll(int sockfd) {
  struct epoll_event new_event;
  int epfd = epoll_create(SOMAXCONN);
  new_event.data.fd = sockfd;
  new_event.events = EPOLLIN;
  epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &new_event);
  return epfd;
}

void SendState(Player* player) {
  char buff[BUFF_SIZE];
  memset(buff, 0, BUFF_SIZE);
  for (size_t i = 0; i < player->word_len; ++i) {
    if (player->known[i]) {
      buff[i] = player->word[i];
    } else {
      buff[i] = '*';
    }
  }
  size_t word_len = player->word_len + 1;
  snprintf(buff + word_len, BUFF_SIZE - word_len, "%u", player->attempts_num);
  size_t attempts_len = strlen(buff + player->word_len + 1);
  write(player->fd, buff, player->word_len + 1 + attempts_len + 1);
}

void AddPlayer(int epfd, int connfd, void* words, size_t words_len, unsigned words_count, unsigned max_attempts) {
  SetNonBlock(connfd);
  struct epoll_event new_event;
  Player* new_player = malloc(sizeof(*new_player));
  char buff[BUFF_SIZE];
  ChooseRandomWord(words, words_len, words_count, buff);
  size_t word_len = strlen(buff);
  new_player->fd = connfd;
  new_player->word_len = word_len;
  new_player->word = calloc(word_len + 1, sizeof(*new_player->word));
  strcpy(new_player->word, buff);
  new_player->known = calloc(word_len, sizeof(*new_player->known));
  new_player->attempts_num = max_attempts;
  new_event.events = EPOLLIN;
  new_event.data.ptr = new_player;
  epoll_ctl(epfd, EPOLL_CTL_ADD, new_player->fd, &new_event);
  SendState(new_player);
}

void RemovePlayer(Player* player, size_t* conns) {
  free(player->word);
  free(player->known);
  shutdown(player->fd, SHUT_RDWR);
  close(player->fd);
  (*conns) -= 1;
  free(player);
}

void HandleGuess(Player* player, size_t* conns) {
  char letter;
  read(player->fd, &letter, sizeof(letter));
  size_t total_known = 0;
  for (size_t i = 0; i < player->word_len; ++i) {
    if (!player->known[i] && player->word[i] == letter) {
      player->known[i] = 1;
    }
    if (player->known[i]) {
      ++total_known;
    }
  }
  --player->attempts_num;
  SendState(player);
  if (player->attempts_num == 0 || total_known == player->word_len) {
    RemovePlayer(player, conns);
  }
}

void HandleEvent(int sockfd, int epfd, size_t* conns, void* words, size_t words_len, unsigned words_count, unsigned max_attempts, struct epoll_event* curr_event) {
  int fd = curr_event->data.fd;
  if (fd == sockfd) {
    if (alive) {
      fd = accept(sockfd, NULL, NULL);
      AddPlayer(epfd, fd, words, words_len, words_count, max_attempts);
      (*conns) += 1;
    } else {
      close(fd);
    }
  } else {
    Player* player = curr_event->data.ptr;
    if ((curr_event->events & EPOLLHUP) == EPOLLHUP || !alive) {
      RemovePlayer(player, conns);
    } else {
      HandleGuess(player, conns);
    }
  }
}

int main(int argc, char** argv) {
  RegisterSAHandler();
  if (argc < 4) {
    puts(USAGE_STR);
    return -1;
  }
  void* words;
  size_t words_len;
  if (PrepareWords(argv[3], &words, &words_len) == -1) {
    puts(NO_WORDS_STR);
    return -1;
  }
  unsigned words_count = CountWords(words, words_len);
  int sockfd = PrepareSocket(argv[1]);
  unsigned max_attempts = atoi(argv[2]);
  int epfd = PrepareEpoll(sockfd);
  struct epoll_event curr_event;
  size_t conns = 0;
  while (alive || conns) {
    epoll_wait(epfd, &curr_event, 1, -1);
    HandleEvent(sockfd, epfd, &conns, words, words_len, words_count, max_attempts, &curr_event);
  }
  close(epfd);
  FreeWords(words);
  return 0;
}
