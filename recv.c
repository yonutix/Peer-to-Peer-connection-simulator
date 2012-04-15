#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10001

typedef unsigned short int word;

int CRC_compare(msg *r, unsigned short int *crc_table);

/**
 * \brief Verifica daca cadrul e corupt
 */
int eval(msg *r, unsigned short int *crc_table)
{
  //return 1;
  return CRC_compare(r, crc_table);
}

/**
 *\return returneaza ACK pentru indexurile de la 1 la numarul maxim al 
 *cadrelor
 */
msg* ACK(int pkg_no)
{
  msg* pkg;
  pkg = (msg*)malloc(sizeof(msg));
  pkg->type = 3;
  sprintf(pkg->payload, "%s\n%d", "ACK", pkg_no);
  pkg->len = strlen(pkg->payload)+1;
  return pkg;
}

/**
 *\return consturieste un NACK
 */
msg* NACK()
{
  msg* pkg;
  pkg = (msg*)malloc(sizeof(msg));
  pkg->type = 3;
  sprintf(pkg->payload, "%s", "NACK");
  pkg->len = strlen(pkg->payload)+1;
  return pkg;
}

/**
 * \brief incarca variabilele cu datele pentru inceput
 */
char* load_specification(msg r, 
  int *frame_no, 
  int *buffer_len)
{
  char* filename;
  filename = (char*)malloc(sizeof(char)*r.len);
  sscanf(r.payload, 
    "%s\n%d\n%d", 
    filename, 
    frame_no, 
    buffer_len);
  return filename;
}

/**
 * \return returneaza continutul util al cadrului
 */
char* get_pkg_content(msg *r)
{
  char* ret;
  int i;
  ret = (char*)malloc(sizeof(char)*r->len);
  for(i = 0; i < r->len; i++){
    ret[i] = r->payload[i];
  }
  return ret;
}

/**
 * \return returneaza indexul frameului, adun cu 0x80 (128) pentru ca initial
 * datele sunt charuri, deci pot fi si negative
 */
word get_msg_index(msg *r)
{
  return ((word)(r->payload[1399]+0x80) + 0x100*(word)(r->payload[1398]+0x80));
}

/**
 * \return returneaza CRC-ul
 */
unsigned short int get_CRC(msg *r)
{
  return (unsigned short int)((r->payload[1396]+128)*0x100+(r->payload[1397]+128));
}

unsigned short int calculcrc (unsigned short int data, unsigned short int acum)
{
  unsigned short int genpoli = 0x3D65;
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

unsigned short int* tabelcrc ()
{
  unsigned short int* ptabelcrc;
  int i;
  if ((ptabelcrc = (unsigned short int*)malloc(256*sizeof(unsigned short int))) == NULL)
     return NULL;
  for (i=0; i<256; i++){
    ptabelcrc[i] = calculcrc (i, 0);
  }
  return ptabelcrc;
}

void crctabel (unsigned short int data, unsigned short int* acum, unsigned short int* tabelcrc)
  {
  unsigned short int valcomb;
  valcomb = ((*acum >> 8) ^ data) & 0x00FF;
  *acum = ((*acum & 0x00FF) << 8) ^ tabelcrc [valcomb];
}

unsigned short int compute_crc(msg *r, unsigned short int *crc_table)
{
  unsigned short int accum = 0;
  int i;
  for(i = 0; i < r->len; i++)
    crctabel((unsigned short int)r->payload[i], &accum, crc_table);

  crctabel((unsigned short int)r->payload[1398], &accum, crc_table);
  crctabel((unsigned short int)r->payload[1399], &accum, crc_table);
  return accum;
}

/**
 * \brief Verifica daca CRC-ul primit si cel calculat sunt egale
 */
int CRC_compare(msg *r, unsigned short int *crc_table)
{
  unsigned short int crc = 0;
  crc = compute_crc(r, crc_table);
  if (crc == get_CRC(r))
    return 1;
  printf("different crc");
  return 0;
}

/**
 * \brief verifica daca c e in intervalul [a, b]
 */
int between(int a, int b, int c){
  return (a <= c && b>= c);
}

int min(int a, int b)
{
  if (a > b)
    return b;
  return a;
}

/**
 * \brief Primul ACK e diferit pentru ca trebuie sa spuna si numarul ferestrei
 */
msg * make_first_ack(int window)
{
  msg* pkg;
  pkg = (msg*)malloc(sizeof(msg));
  pkg->type = 3;
  sprintf(pkg->payload, "%s\n%d\n%d", "ACK", 0, window);
  pkg->len = strlen(pkg->payload)+1;
  return pkg;
}

int main(int argc,char** argv){
  msg *r, **small_buff;
  init(HOST,PORT);
  char *filename;
  int length, count = -1, i, null_inx, window=0, win, fd;
  /*Genereaza tabelul pentru CRC*/
  word *crc_table = tabelcrc();
  if(argc > 1){
    sscanf(argv[1], "window=%d", &window);
  }
  if(win != 0){
    small_buff = 
      (msg**)malloc(sizeof(msg*)*window);
  }
  else{
    /*Daca window este 0 presupun un buffer de dimensiune 100*/
    small_buff = 
      (msg**)malloc(sizeof(msg*)*100);
  }
  /*Initializare buffer*/
  for(i = 0; i < window; i++)
    small_buff[i] = NULL;
  
  while(1){
    /*Astept primirea unui mesaj*/
    r = receive_message();
    if(eval(r, crc_table)){
      /*Un pachet valid(necorupt) a fost primit*/
      if(get_msg_index(r) == 0 && count == -1){
        /*Primul pachet(headerul)*/
        filename = load_specification(*r, &length, &win);
        fd = open(filename,O_WRONLY|O_CREAT,0644);
        if (fd<0) {
          perror("Failed to open file\n");
        }
        if(window == 0)
          window = win;
        send_message(make_first_ack(window));
        count++;
      }
      else{
        /*Un pachet cu indexul mai mare ca 0 a fost rpimit*/
        if(get_msg_index(r) == count + 1){
          /*Daca pachetul rpimit e cel asteptat*/
            count++;
            write(fd, get_pkg_content(r), r->len);
            /*Golire buffer pana la urmatoarea pozitie nula*/
            for(i = 0; i < win; i++){
              if(small_buff[i] == NULL)
                break;
              count++;
              write(fd, get_pkg_content(small_buff[i]),small_buff[i]->len);
            }
            null_inx = i;
            /*Operatie de deplasare spre stanga cu cate elemente au fost scoase
            din buffer*/
            for(i = null_inx; i < win; i++){
              small_buff[i-null_inx] = small_buff[i];
            }
            /*ultimele elemente sunt intializate cu 0 pentru ca au fost multate*/
            for(i = win - null_inx; i < win; i++){
              small_buff[i] = NULL;
            }
            /*Trimite confirmare*/
          send_message(ACK(count));
          if(count == length-1)
            break;
        }
        else{
          /*Daca pachetul nu a venit in ordine este adaugat in buffer*/
          if(between(count+1, count+win, get_msg_index(r))){
            small_buff[get_msg_index(r) - count+1] = r;
          }
          /*Daca pachetul sosit nu este cel asteptat inseamna ca cel asteptat
          s-a pierdut, asa ca trimit NACK*/
          send_message(NACK());
        } 
      }
    }
    else{
      /*Un pachet corupt a fost primit*/
      send_message(NACK());
    }
  }
  //free(small_buff);
  close(fd);
  return 0;
}
