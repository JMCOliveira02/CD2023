/*Non-Canonical Input Processing*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// #define CASA

#define BAUDRATE B38400
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1
#define BUFFER_SIZE 8
volatile int STOP = FALSE;

typedef enum
{
    Start,
    F_RCV,
    A_RCV,
    C_RCV,
    BCC_RCV,
    Stop
} RCV_SET_state;

const char F = 0x5C;
const char A = 0x01;
const char C_SET = 0x03;
const char C_UA = 0x05;
const char C_DISC = 0x0B;
const char Bcc_SET = A ^ C_SET;
const char Bcc_UA = A ^ C_UA;
const char Bcc_DISC = A ^ C_DISC;

const char SET[5] = {F, A, C_SET, Bcc_SET, F};
const char UA[5] = {F, A, C_UA, Bcc_UA, F};
const char DISC[5] = {F, A, C_DISC, Bcc_DISC, F};

struct termios oldtio, newtio;

int fd;
int conta = 0, flag = 1;

RCV_SET_state current_state = Start;

int menu();
int llopen(char *porta, int receiver);
void get_send_confirm(struct termios oldtio);
void read_respond(struct termios oldtio);
void maquina(unsigned char *buf, const char *Frame);
void escreve();
int send_SET_wait_UA();
void read_SET_send_UA();
int llopen(char *porta, int receiver);
int llclose(char *porta, int receiver);

int main(int argc, char **argv)
{
    int c, res;
    int i;

#ifdef CASA
    if ((argc < 2) ||
        ((strcmp("/dev/ttyS10", argv[1]) != 0) &&
         (strcmp("/dev/ttyS11", argv[1]) != 0)))
    {
        printf("Usage:\t./noncanonical_all SerialPort \n\t\t\t\t/dev/ttyS10 -> transmitter\n\t\t\t\t/dev/ttyS11 -> receiver\n\n");
        exit(1);
    }
#else
    if ((argc < 2) ||
        ((strcmp("/dev/ttyS0", argv[1]) != 0) &&
         (strcmp("/dev/ttyS1", argv[1]) != 0)))
    {
        printf("Usage:\t./noncanonical_all SerialPort \n\t\t\t\t/dev/ttyS0");
        exit(1);
    }
#endif

    int r = menu();

    int connection = llopen(argv[1], r); // 0 for transmitter, 1 for receiver

    if (connection == 0 && !r)
    {
        printf("Não foi possível estabelecer conexão com Receiver!\n\n");
        exit(-1);
    }

    if (r)
    {
        read_respond(oldtio);
        llclose(argv[1], r);
    }
    else
    {
        get_send_confirm(oldtio);
        llclose(argv[1], r);
    }

    return 0;
}

int menu()
{
    printf("--------MENU---------\n\nPress 0 for transmitter and 1 for receiver\n\n");
    int r;
    char c;
    while (1)
    {
        scanf("%d", &r);
        scanf("%c", &c);
        if (r == 1 || r == 0)
            break;
        printf("invalid input!  %d\n\n\n", r);
    }

    printf("\033[H\033[J");

    return r;
}

void get_send_confirm(struct termios oldtio)
{
    char buf[255], buf2[255];
    printf("\n------Transmitter mode------\n\nWrite message to send: ");

    fgets(buf, 255, stdin);
    buf[strlen(buf) - 1] = '\0';
    int res = write(fd, buf, 255);
    printf("%d bytes written\n", res);

    char aux[255] = "";

    while (STOP == FALSE)
    {                            /* loop for input */
        res = read(fd, buf2, 8); /* returns after 8 chars have been input */
        buf2[res] = 0;           /* so we can printf... */
        printf("{ %s } %d bytes read\n", buf2, res);
        strcat(aux, buf2);

        for (int i = 0; i < 8; i++)
        {
            if (buf2[i] == '\0')
                STOP = TRUE;
            break;
        }
    }

    puts(aux);

    if (strcmp(aux, buf) == 0)
    {
        printf("%s ==> %s\n", aux, "GREAT SUCCESS! NO ERRORS\n");
    }

    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }
}

void read_respond(struct termios oldtio)
{
    unsigned char aux[255] = "";
    char buf[255];

    printf("\n------Receiver mode------\n\nWaiting for message...\n\n");

    while (STOP == FALSE)
    {                               /* loop for input */
        int res = read(fd, buf, 8); /* returns after 8 chars have been input */
        buf[res] = 0;
        strcat(aux, buf);

        printf("{ %s } %d bytes read\n", buf, res); /* so we can printf... */

        for (int i = 0; i < 8; i++)
        {
            if (buf[i] == '\0')
                STOP = TRUE;
        }
    }
    printf("Received message: ");
    printf("%s\n", aux);
    sleep(1);
    write(fd, aux, 255);

    tcsetattr(fd, TCSANOW, &oldtio);
}

void maquina(unsigned char *buf, const char *Frame)
{
    int i = 0;
    switch (current_state)
    {
    case Start:
        if (buf[0] == Frame[0])
        {
            current_state = F_RCV;
        }
        break;

    case F_RCV:
        if (buf[0] == Frame[1])
        {
            current_state = A_RCV;
        }
        else if (buf[0] != Frame[0])
        {
            current_state = Start;
        }
        break;

    case A_RCV:
        if (buf[0] == Frame[0])
        {
            current_state = F_RCV;
        }
        else if (buf[0] == Frame[2])
        {
            current_state = C_RCV;
        }
        else
        {
            current_state = Start;
        }
        break;

    case C_RCV:
        if (buf[0] == Frame[0])
        {
            current_state = F_RCV;
        }
        else if (buf[0] == Frame[1] ^ Frame[2])
        {
            current_state = BCC_RCV;
        }
        else
        {
            current_state = Start;
        }
        break;

    case BCC_RCV:
        if (buf[0] == Frame[4])
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

void escreve()
{
    (void)signal(SIGALRM, escreve);

    write(fd, SET, 5);

    if (conta < 3)
    {
        alarm(3);
    }
    else
    {
        printf("Não foi possível estabelecer conexão com Receiver!\n\n");
        exit(-1);
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

int send_SET_wait_UA()
{

    char buf[1];

    escreve();

    while (1)
    {
        read(fd, buf, 1);
        maquina(buf, UA);

        if (current_state == Stop)
        {
            printf("UA recebido!\n");
            current_state = Start;
            alarm(0);
            return 1;
            break;
        }
    }
    return 0;
}

void read_SET_send_UA()
{
    unsigned char buf[1];

    printf("Espera por SET...\n");

    while (current_state != Stop)
    {
        read(fd, buf, 1);
        maquina(buf, SET);
    }

    printf("SET recebido! A enviar UA...\n");

    current_state = Start;
    write(fd, UA, 5);
}

int llopen(char *porta, int receiver)
{
    fd = open(porta, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(porta);
        exit(-1);
    }

    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 0; /* inter-character timer unused */
    newtio.c_cc[VMIN] = 1;  /* blocking read until 5 chars received */

    tcflush(fd, TCIOFLUSH);

    sleep(1);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");
    if (receiver)
    {
        read_SET_send_UA();
    }
    else
    {
        return send_SET_wait_UA();
    }

    return 1;
}

int llclose(char *porta, int receiver)
{
    if (receiver)
    {
        unsigned char buf[1];

        printf("Espera por DISC...\n");

        while (current_state != Stop)
        {
            read(fd, buf, 1);
            maquina(buf, DISC);
        }

        printf("DISC recebido! A enviar DISC...\n");

        current_state = Start;
        write(fd, DISC, 5);

        printf("DISC enviado! Espera por UA...\n");

        while (current_state != Stop)
        {
            read(fd, buf, 1);
            maquina(buf, UA);
        }

        current_state = Start;

        printf("UA recebido!\n A fechar conexão\n");
        close(fd);
        exit(-1);
    }
    else
    {
        char buf[1];

        write(fd, DISC, 5);

        printf("DISC enviado! Espera por DISC...\n");

        while (current_state != Stop)
        {
            read(fd, buf, 1);
            maquina(buf, DISC);
        }
        current_state = Start;

        printf("DISC recebido!\n A enviar UA e fechar conexão\n");

        write(fd, UA, 5);
        close(fd);
        exit(-1);
    }
}