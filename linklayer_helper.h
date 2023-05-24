#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>

struct termios oldtio, newtio;

struct timeval inicio, fim;
double t_transf; 

typedef struct linkLayer
{
    char serialPort[50];
    int role; // defines the role of the program: 0==Transmitter, 1=Receiver
    int baudRate;
    int numTries;
    int timeOut;
} linkLayer;

// ROLE
#define NOT_DEFINED -1
#define TRANSMITTER 0
#define RECEIVER 1

// SIZE of maximum acceptable payload; maximum number of bytes that application layer should send to link layer
#define MAX_PAYLOAD_SIZE 1000

// CONNECTION deafault values
#define BAUDRATE_DEFAULT B38400
#define MAX_RETRANSMISSIONS_DEFAULT 3
#define TIMEOUT_DEFAULT 4
#define _POSIX_SOURCE 1 /* POSIX compliant source */

// MISC
#define FALSE 0
#define TRUE 1

typedef enum
{
    Start,
    F_RCV,
    A_RCV,
    C_RCV,
    BCC_RCV,
    Stop
} RCV_SET_state;

typedef enum
{
    Start_I,
    F_RCV_I,
    A_RCV_I,
    C_RCV_I0,
    C_RCV_I1,
    Stop_I0,
    Stop_I1
} RCV_I_state;

typedef enum
{
    Start_RR,
    F_RCV_RR,
    A_RCV_RR,
    C_RCV_RR0,
    C_RCV_RR1,
    BCC_RCV_RR0,
    BCC_RCV_RR1,
    Stop_RR0,
    Stop_RR1
} RCV_RR_state;



typedef enum
{
    set,
    msg,
    clo
} flag_alarme;

RCV_SET_state current_state = Start;
RCV_I_state current_state_I = Start_I;
RCV_RR_state current_state_RR = Start_RR;
flag_alarme flag = set;

const char F = 0x5C;
const char A = 0x01;
const char C_SET = 0x03;
const char C_UA = 0x05;
const char C_DISC = 0x0B;
const char Bcc_SET = A ^ C_SET;
const char Bcc_UA = A ^ C_UA;
const char Bcc_DISC = A ^ C_DISC;
char Bcc_I2 = 0x00;

const char S_0 = 0x00;
const char S_1 = 0x02;

const char R_0 = 0x01;
const char R_1 = 0x21;

const char REJ_0 = 0x05;
const char REJ_1 = 0x25;

const char SET[5] = {F, A, C_SET, Bcc_SET, F};
const char UA[5] = {F, A, C_UA, Bcc_UA, F};
const char DISC[5] = {F, A, C_DISC, Bcc_DISC, F};

const char RR_0[5] = {F, A, R_0, A ^ R_0, F};
const char RR_1[5] = {F, A, R_1, A ^ R_1, F};

char RR[2][5] = {{F, A, R_0, A ^ R_0, F},
                 {F, A, R_1, A ^ R_1, F}};

const char I_0[5] = {F, A, S_0, A ^ S_0, F};
const char I_1[5] = {F, A, S_1, A ^ S_1, F};

int fd;
int conta = 0;
int show;

int timeout, num_tries;
int iteracao = 0;
int nbytes = 0;
int prev_S = 1, cur_S;
int rcv_RR;
bool sent_RR = 0;

const int FER = 10;
int NER = 100/FER;
int a = 1;

unsigned char buf_aux[2 * MAX_PAYLOAD_SIZE + 1];
unsigned char message[2 * MAX_PAYLOAD_SIZE + 6];
unsigned char buffer1[2 * MAX_PAYLOAD_SIZE + 6];

int length_aux, length_final, length_escreve;

// Waits for SET control message and sends back UA when SET is received
void read_SET_send_UA();
// Sends SET control message and waits for UA (timeout per SET sent = 3 seconds; number of tries before error = 3)
int send_SET_wait_UA();
// State machine to read Frames from buffer
void maquina(unsigned char buf, const char *Frame);
// Sends the requested message every "timeout" seconds and exits after "numTries" consecutive tries
void escreve();
void maquina_I(unsigned char buf);
void maquina_RR(unsigned char buf);
void msg_final();
int llwrite(char *buffer, int length);

void maquina(unsigned char buf, const char *Frame)
{
    int i = 0;
    switch (current_state)
    {
    case Start:
        if (buf == Frame[0])
        {
            current_state = F_RCV;
        }
        break;

    case F_RCV:
        if (buf == Frame[1])
        {
            current_state = A_RCV;
        }
        else if (buf != Frame[0])
        {
            current_state = Start;
        }
        break;

    case A_RCV:
        if (buf == Frame[0])
        {
            current_state = F_RCV;
        }
        else if (buf == Frame[2])
        {
            current_state = C_RCV;
        }
        else
        {
            current_state = Start;
        }
        break;

    case C_RCV:
        if (buf == Frame[0])
        {
            current_state = F_RCV;
        }
        else if (buf == Frame[1] ^ Frame[2])
        {
            current_state = BCC_RCV;
        }
        else
        {
            current_state = Start;
        }
        break;

    case BCC_RCV:
        if (buf == Frame[4])
        {
            current_state = Stop;
        }
        else
        {
            current_state = Start;
        }
        break;
    }
}

void maquina_I(unsigned char buf)
{

    switch (current_state_I)
    {
    case Start_I:
        if (buf == F)
        {
            current_state_I = F_RCV_I;
        }
        break;

    case F_RCV_I:
        if (buf == A)
        {
            current_state_I = A_RCV_I;
        }
        else if (buf != F)
        {
            current_state_I = Start_I;
        }
        break;

    case A_RCV_I:
        if (buf == F)
        {
            current_state_I = F_RCV_I;
        }
        else if (buf == I_0[2])
        {
            current_state_I = C_RCV_I0;
        }
        else if (buf == I_1[2])
        {
            current_state_I = C_RCV_I1;
        }
        else
        {
            current_state_I = Start_I;
        }
        break;

    case C_RCV_I0:
        if (buf == F)
        {
            current_state_I = F_RCV_I;
        }
        else if (buf == I_0[1] ^ I_0[2])
        {
            current_state_I = Stop_I0;
        }
        else
        {
            current_state_I = Start_I;
        }
        break;

    case C_RCV_I1:
        if (buf == F)
        {
            current_state_I = F_RCV_I;
        }
        else if (buf == I_1[1] ^ I_1[2])
        {
            current_state_I = Stop_I1;
        }
        else
        {
            current_state_I = Start_I;
        }
        break;
    }
}

void maquina_RR(unsigned char buf)
{
    switch (current_state_RR)
    {
    case Start_RR:
        if (buf == F)
        {
            current_state_RR = F_RCV_RR;
        }
        break;

    case F_RCV_RR:
        if (buf == A)
        {
            current_state_RR = A_RCV_RR;
        }
        else if (buf != F)
        {
            current_state_RR = Start_RR;
        }
        break;

    case A_RCV_RR:
        if (buf == F)
        {
            current_state_RR = F_RCV_RR;
        }
        else if (buf == RR_0[2])
        {
            current_state_RR = C_RCV_RR0;
        }
        else if (buf == RR_1[2])
        {
            current_state_RR = C_RCV_RR1;
        }
        else
        {
            current_state_RR = Start_RR;
        }
        break;

    case C_RCV_RR0:
        if (buf == F)
        {
            current_state_RR = F_RCV_RR;
        }
        else if (buf == RR_0[1] ^ RR_0[2])
        {
            current_state_RR = BCC_RCV_RR0;
        }
        else
        {
            current_state_RR = Start_RR;
        }
        break;

    case C_RCV_RR1:
        if (buf == F)
        {
            current_state_RR = F_RCV_RR;
        }
        else if (buf == RR_1[1] ^ RR_1[2])
        {
            current_state_RR = BCC_RCV_RR1;
        }
        else
        {
            current_state_RR = Start_RR;
        }
        break;

    case BCC_RCV_RR0:
        if (buf == F)
        {
            current_state_RR = Stop_RR0;
        }
        else
        {
            current_state_RR = Start_RR;
        }
        break;

    case BCC_RCV_RR1:
        if (buf == F)
        {
            current_state_RR = Stop_RR1;
        }
        else
        {
            current_state_RR = Start_RR;
        }
        break;
    }
}

void escreve()
{
    (void)signal(SIGALRM, escreve);

    if (flag == set)
    {
        write(fd, SET, length_escreve);

        if (conta < num_tries)
        {
            alarm(timeout);
        }
        else
        {
            printf("Não foi possível estabelecer conexão com Receiver!\n\n");
            conta = 0;
            return;
        }

        if (conta == 0)
        {
            printf("A enviar SET...\n");
        }
        else
        {
            printf("timeout! A enviar SET novamente...(tentativa %d)\n", conta + 1);
        }
        conta++;
        return;
    }

    if (flag == msg)
    {
        printf("\nA enviar com S=%d\n", sent_RR);
        write(fd, message, length_escreve);
        if (conta < num_tries)
        {
            alarm(timeout);
        }
        else
        {
            printf("Confirmação da mensagem não foi recebida!\n\n");
            conta = 0;
            exit(-1);
        }

        if (conta == 0)
        {
            printf("A enviar mensagem...\n");
        }
        else
        {
            printf("timeout! A enviar mensagem novamente...(tentativa %d)\n", conta + 1);
        }
        conta++;
        return;
    }

    if (flag == clo)
    {
        write(fd, DISC, length_escreve);

        if (show)
            printf("DISC enviado! Espera por DISC...\n");

        if (conta < num_tries)
        {
            alarm(timeout);
        }
        else
        {
            if (show)
                printf("Confirmação do DISC não foi recebida!\n\n");
            conta = 0;
            exit(-1);
        }

        if (conta == 0)
        {
            if (show)
                printf("A enviar mensagem...\n");
        }
        else
        {
            if (show)
                printf("timeout! A enviar mensagem novamente...(tentativa %d)\n", conta + 1);
        }
        conta++;
        return;
    }

    return;
}

void read_SET_send_UA()
{
    unsigned char buf;

    printf("Espera por SET...\n");

    while (current_state != Stop)
    {
        read(fd, &buf, 1);
        maquina(buf, SET);
    }

    printf("SET recebido! A enviar UA...\n");

    current_state = Start;
    write(fd, UA, 5);
}

int send_SET_wait_UA()
{
    unsigned char buf;

    flag = set;
    length_escreve = 5;
    escreve();

    while (1)
    {
        read(fd, &buf, 1);
        maquina(buf, UA);

        if (current_state == Stop)
        {
            printf("UA recebido!\n");
            current_state = Start;
            conta = 0;
            alarm(0);
            return 1;
        }
    }
}

void msg_final()
{

    message[0] = F;
    message[1] = A;

    if (sent_RR == 0)
    {
        message[2] = S_0;
    }
    else
    {
        message[2] = S_1;
    }

    message[3] = message[1] ^ message[2];

    for (int i = 0; i < length_aux; i++)
    {
        message[i + 4] = buf_aux[i];
    }
    message[length_aux + 4] = F;
    length_final = length_aux + 5;
}
