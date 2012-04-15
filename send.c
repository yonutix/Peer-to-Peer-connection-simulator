#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <signal.h>
#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10000

static int alarm_armed;
static void (*alarm_saved_handler)(int);

typedef unsigned short int word;
static void alarm_handler(int sig)
{
  printf("Alarm handler");
  alarm_armed=1;
}

const int * set_alarm(unsigned int timeout)
{
  alarm_armed=0;
  alarm(timeout);
  return &alarm_armed;
}

int init_alarm()
{
  alarm_saved_handler=signal(SIGALRM, alarm_handler);
  siginterrupt(SIGALRM, 1);
  return 0;
}

int get_file_size(char* filename)
{
  struct stat buf;
  if(stat(filename, &buf)<0)
    return -1;
  return (int)buf.st_size;
}

int to_int(char* str)
{
  return (int)strtol(str, NULL, 10);
}

int to_float(char* str)
{
  return (float)strtol(str, NULL, 10);
}

/**
 * \brief Folosesc polinomul 0x13D65, CRC-DNP
 */
unsigned short int calculcrc (word data, word acum)
{
  word genpoli = 0x3D65;
  int i;
  data <<= 8;
  for (i=8; i>0; i--){
     if ((data^acum) & 0x8000)
       acum = (acum << 1) ^ genpoli;
     else
       acum <<= 1;
     data <<= 1;
  }
  return acum;
}

word* tabelcrc ()
{
  word* ptabelcrc;
  int i;
  if ((ptabelcrc = (word*)malloc(256*sizeof(word))) == NULL)
     return NULL;
  for (i=0; i<256; i++){
    ptabelcrc[i] = calculcrc (i, 0);
  }
  return ptabelcrc;
}


void crctabel (word data, word* acum, word* tabelcrc)
  {
  word valcomb;
  valcomb = ((*acum >> 8) ^ data) & 0x00FF;
  *acum = ((*acum & 0x00FF) << 8) ^ tabelcrc [valcomb];
}

word compute_crc(msg r, word *crc_table)
{
  word accum = 0;;
  int i;
  for(i = 0; i < r.len; i++)
    crctabel((word)r.payload[i], &accum, crc_table);

  crctabel((word)r.payload[1398], &accum, crc_table);
  crctabel((word)r.payload[1399], &accum, crc_table);
  return accum;
}

/**
 * \brief Incarca datele intr-un buffer pentru a avea acces mai usor la ele cand
 * sunt mutate in bufferul mai mic.
 * \param @train : contine datele impartite in cadre
 * \param @data_size : continutul util dintr-un cadru
 * \param @transport_name : numele fisierului
 * \param @buffer_len :dimensiunea ferestrei
 * \return : numarul cadrelor
 */
int load_data(msg **train, int data_size, char* transport_name, int buffer_len)
{
  int i, 
  vagons_no, 
  fd;
  msg *vagons;
  vagons_no = 
    ceil((float)get_file_size(transport_name)/((float)data_size)) + 1;

  if(get_file_size(transport_name) == -1){
    perror("file size couldn't be established\n");
    return -1;
  }
  vagons = (msg*)malloc(sizeof(msg) * vagons_no);

  fd = open(transport_name, O_RDONLY);

  if(fd < 0){
    perror("Couldn't open file\n");
    return -1;
  }
  /*Pentru a nu fi calculat crc de fiecare data pentru fiecare octet, se pastreaza
  intr-un vector rezultatele pentru fiecare din cele 256 cazuri*/
  unsigned short int *crc_table = tabelcrc();
  unsigned short int crc = 0;
  
  char* recv_filename;
  recv_filename = (char*)malloc(sizeof(char)*strlen(transport_name)+6);

  strcpy(recv_filename, "recv_");
  strcat(recv_filename, transport_name);

  sprintf(vagons[0].payload, 
    "%s\n%d\n%d", 
    recv_filename, 
    vagons_no, 
    buffer_len);
  vagons[0].len = strlen(vagons[0].payload);
  vagons[0].payload[1398] = vagons[0].payload[1399] = -128;
  crc = compute_crc(vagons[0], crc_table);
  vagons[0].payload[1396] = (char)(crc/0x100-128);
  vagons[0].payload[1397] = (char)(crc%0x100-128);
  //vagons
  for(i = 1; i < vagons_no; i++){
    vagons[i].type = 2;
    vagons[i].len = read(fd, &vagons[i].payload, data_size);
    vagons[i].payload[1398] = (char)(i/0x100-128);
    vagons[i].payload[1399] = (char)(i%0x100-128);

    crc = compute_crc(vagons[i], crc_table);
    vagons[i].payload[1396] = (char)(crc/0x100-128);
    vagons[i].payload[1397] = (char)(crc%0x100-128);
  }
  close(fd);
  *train = vagons;
  return vagons_no;
}

/**
 * \return 0: is corrupt , 1: is not
 */
int eval(msg *r)
{
  if(r){
    //TODO:crc, pentru cazul in care legatura reciever-sender nu ar fi ideala
    //aici nu e cazul
    return 1;
  }
  else{
    return 0;
  }
}

int min(int a, int b)
{
  if (a > b)
    return b;
  return a;
}

/**
 * \return returneaza indexul unui cadru
 */
word get_msg_index(msg *r)
{
  return ((word)(r->payload[1399]+0x80) + 0x100*((word)r->payload[1398]+0x80));
}

/**
 * \brief verifica daca mesajul e ACK
 * se preupune ca mesajul nu este null
 */
int is_ACK(msg *r)
{
  char *aux_string;
  aux_string = (char*)malloc(sizeof(char)*4);
  int ack_inx;
  if(r->payload[0] == 'A')
  {
    ack_inx = to_int(r->payload+4);
    //TODO:free aux_string
    return ack_inx;
  }
  return -1;
}

/**
 * \brief functia care controleaza trimiterea 
 * alarma e utila in cazul in care legatura reciver-sender nu e ideala
 */
void send_file(char* filename, int speed, int delay){
  //constant for now
  msg *container, *r;
  int data_size = 1396, 
    length, 
    local_count = -1, 
    count = -1
    ,alarm_timeout = 5;
    int buffer_len, end;
  const int *p;
  /*buffer_len : numarul de pachete care pot fi trimise fara a astepta ACK*/
  buffer_len = ceil((float)(speed*1024)/((float)sizeof(msg)/1024) * 0.001*(float)delay);
  /*numarul total de pachete*/
  length = load_data(&container, data_size, filename, buffer_len);
  end = min(length-1, buffer_len-1);
  init_alarm();
  p = set_alarm(alarm_timeout);
  while(1){
    local_count++;
    /*daca alarma suna*/
    if(!p){
      local_count = count+1;
      p = set_alarm(alarm_timeout);
    }
    send_message(&container[local_count]);
    /*primeste mesajul*/
    r = receive_message_timeout(3);
    /*analizeaza ACK sau NACK*/
    if(eval(r)){
      /*If a message has been recived*/
      if(is_ACK(r) != -1)
      {
        /*Daca un ACK a fost primit*/
        count = is_ACK(r);
        if(count >= local_count)
          local_count = count + 1;
        alarm(0);
        p = set_alarm(alarm_timeout);
        /*sincronizeaza bufferele*/
        if(is_ACK(r) == 0){
          /*Primul ACK contine si dimensiunea bufferului reciver-ului*/
          if(to_int(r->payload+5)  != 0){
            /*Extrage dimensiunea ferestrei*/
            buffer_len = min(to_int(r->payload+5), buffer_len);
          }
          else{
            /*Daca marimea ferestrei e 0 in reciver presupun un buffer de 
            dimensiune 100*/
            buffer_len = min(100, buffer_len);
          }
        }
        /*Deplaseaza bufferul*/
        end = count + buffer_len;
      }
      else{
        /*Daca a fost primit un NACK*/
        local_count = count;
        alarm(0);
        p = set_alarm(alarm_timeout);
      } 
    }
    if(local_count == end)
      local_count = count;
    if(count == length-1){
      /*all frames has been sended and recived*/
      break;
    }
  }
  /*Elibereaza memoria*/
  free(container);
  /*Sterge alarma*/
  signal(SIGALRM, alarm_saved_handler);
}

int main(int argc,char** argv){
  init(HOST,PORT);
  if (argc<2){
    printf("Usage %s filename\n",argv[0]);
    return -1;
  }
  send_file(argv[5], to_int(argv[1]), to_int(argv[2]));
  return 0;
}
