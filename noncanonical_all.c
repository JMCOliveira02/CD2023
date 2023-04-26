/*Non-Canonical Input Processing*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BAUDRATE B38400
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1
#define BUFFER_SIZE 255

volatile int STOP = FALSE;

void get_send_confirm(int fd, struct termios oldtio);
void read_respond();

int main(int argc, char **argv)
{
    int fd, c, res;
    struct termios oldtio, newtio;
    int i, sum = 0, speed = 0;

    if ((argc < 2) ||
        ((strcmp("/dev/ttyS10", argv[1]) != 0) &&
         (strcmp("/dev/ttyS11", argv[1]) != 0)))
    {
        printf("Usage:\t./noncanonical_all SerialPort \n\t\t\t\t/dev/ttyS10 transmitter\n\t\t\t\t/dev/ttyS11 receiver\n\n");
        exit(1);
    }

    /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
    */

    fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(argv[1]);
        exit(-1);
    }

    if (tcgetattr(fd, &oldtio) == -1)
    { /* save current port settings */
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

    /*
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
    leitura do(s) próximo(s) caracter(es)
    */

    tcflush(fd, TCIOFLUSH);

    sleep(1);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    if (strcmp(argv[1], "/dev/ttyS10") == 0)
    {
        get_send_confirm(fd, oldtio);
    }

    if (strcmp(argv[1], "/dev/ttyS11") == 0)
    {
        read_respond(fd, oldtio);
    }

    return 0;
}

void get_send_confirm(int fd, struct termios oldtio)
{

    char buf[255], buf2[255];
    printf("------Transmitter mode------\n\nWrite message to send: ");

    fgets(buf, BUFFER_SIZE, stdin);

    // printf("%s\n", buf);
    int res = write(fd, buf, BUFFER_SIZE);
    printf("%d bytes written\n", res);

    char aux[BUFFER_SIZE] = "";

    while (STOP == FALSE)
    {                                      /* loop for input */
        res = read(fd, buf2, BUFFER_SIZE); /* returns after 255 chars have been input */
        // buf2[res] = 0;             /* so we can printf... */
        printf(":  %s  : %d\n", buf2, res);
        strcat(aux, buf2);
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            if (buf2[i] == '\0')
                STOP = TRUE;
        }
    }
    // printf("%s\n", aux);

    if (strcmp(aux, buf) == 0)
    {
        printf("%s : %s\n", aux, "SUCCESS: NO ERRORS\n");
    }

    /*
    O ciclo FOR e as instruções seguintes devem ser alterados de modo a respeitar
    o indicado no guião
    */

    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);
}

void read_respond(int fd, struct termios oldtio)
{
    unsigned char aux[BUFFER_SIZE] = "";
    char buf[255];

    printf("------Receiver mode------\n\nWaiting for message...\n\n");

    while (STOP == FALSE)
    {                                         /* loop for input */
        int res = read(fd, buf, BUFFER_SIZE); /* returns after 1 char have been input */
        strcat(aux, buf);

        printf("%s   %d bytes read\n", buf, res); /* so we can printf... */

        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            if (buf[i] == '\0')
                STOP = TRUE;
        }
    }
    puts("Received message: ");
    puts(aux);
    sleep(1);
    write(fd, aux, BUFFER_SIZE);

    /*
    O ciclo WHILE deve ser alterado de modo a respeitar o indicado no guião
    */

    tcsetattr(fd, TCSANOW, &oldtio);
    close(fd);
}
