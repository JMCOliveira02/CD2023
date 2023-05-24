#ifndef LINKLAYER
#define LINKLAYER

#include "linklayer_helper.h"
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

// Opens a connection using the "port" parameters defined in struct linkLayer, returns "-1" on error and "1" on sucess
int llopen(linkLayer connectionParameters);
// Sends data in buf with size bufSize
int llwrite(char *buf, int bufSize);
// Receive data in packet
int llread(char *packet);
// Closes previously opened connection; if showStatistics==TRUE, link layer should print statistics in the console on close
int llclose(linkLayer connectionParameters, int showStatistics);

int llopen(linkLayer connectionParameters)
{
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    timeout = connectionParameters.timeOut;
    num_tries = connectionParameters.numTries;

    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        exit(-1);
    }

    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...)*/
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 0; /* inter-character timer unused*/
    newtio.c_cc[VMIN] = 1;  /* blocking read until 5 chars received*/

    tcflush(fd, TCIOFLUSH);

    sleep(1);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");
    if (connectionParameters.role)
    {
        read_SET_send_UA();
    }
    else
    {
        int b = send_SET_wait_UA();
        gettimeofday(&inicio, NULL);
        return b;
    }

    gettimeofday(&inicio, NULL);
    return 1;
}

int llwrite(char *buffer, int length)
{
    int i = 0, i_aux = 0;

    for (i = 0; i < length; i++) // ################################################# //
    {
        Bcc_I2 ^= buffer[i];

        if (buffer[i] == 0x5c || buffer[i] == 0x5d)
        {
            buf_aux[i_aux] = 0x5d;
            i_aux++;
            buf_aux[i_aux] = buffer[i] ^ 0x20;
            i_aux++;
        }
        else
        {
            buf_aux[i_aux] = buffer[i];
            i_aux++;
        }
    }

    if (Bcc_I2 == 0x5c || Bcc_I2 == 0x5d)
    {
        buf_aux[i_aux] = 0x5d;
        i_aux++;
        buf_aux[i_aux] = Bcc_I2 ^ 0x20;
        i_aux++;
    }
    else // ################################################# //
    {
        buf_aux[i_aux] = Bcc_I2;
        i_aux++;
    }

    length_aux = i_aux;

    msg_final();

    flag = msg;
    length_escreve = length_final;
    escreve();

    char buf;
    current_state_R = Start_R;

    while (1) // ciclo para calcular N(R) na trama
    {
        read(fd, &buf, 1);
        maquina_R(buf);

        if (current_state_R == Stop_RR0)
        {
            printf("RR recebido (RR=0)\n");
            rcv_RR = 0;
            current_state_R = Start_R;
            if (rcv_RR != sent_RR)
            {
                break;
            }
        }
        if (current_state_R == Stop_RR1)
        {
            printf("RR recebido (RR=1)\n");
            rcv_RR = 1;
            current_state_R = Start_R;
            if (rcv_RR != sent_RR)
            {
                break;
            }
        }
        if (current_state_R == Stop_REJ0)
        {
            printf("REJ recebido (REJ=0)\n");
            current_state_R = Start_R;
            alarm(0);
            conta--;
            escreve();
            continue;
        }
        if (current_state_R == Stop_REJ1)
        {
            printf("REJ recebido (REJ=1)\n");
            current_state_R = Start_R;
            alarm(0);
            conta--;
            escreve();
            continue;
        }
    }

    sent_RR = !sent_RR;
    alarm(0);
    conta = 0;
    Bcc_I2 = 0x00;
    // usleep(distancia_km * 5); // distancia
    return length_aux;
}

int llread(char *packet)
{
    int i_b1 = 0;
    unsigned char buf;
    char Bcc_teste = 0x00;

    while (1)
    {

        while (1) // ciclo para calcular N(S) na trama
        {
            read(fd, &buf, 1);
            maquina_I(buf);

            if (current_state_I == Stop_I0)
            {
                printf("\nControlo recebido (S=0)\n");
                cur_S = 0;
                current_state_I = Start_I;
                if (cur_S == prev_S)
                {
                    printf("S errado, a enviar RR(1)\n");
                    usleep(distancia_km * 5);
                    write(fd, RR_1, 5);
                    continue;
                }
                else
                    break;
            }

            if (current_state_I == Stop_I1)
            {
                printf("\nControlo recebido (S=1)\n");
                cur_S = 1;
                current_state_I = Start_I;
                if (cur_S == prev_S)
                {
                    printf("S errado, a enviar RR(0)\n");
                    usleep(distancia_km * 5);
                    write(fd, RR_0, 5);
                    continue;
                }
                else
                    break;
            }

            if (current_state_I == WRONG_BCC)
            {
                printf("BCC errado, a enviar REJ(%d)\n", wrong_S);
                usleep(distancia_km * 5);
                write(fd, REJ[wrong_S], 5);
            }
        }

        printf("O S é o correto\n");

        while (1) // ciclo para o destuffing e o teste do Bcc2
        {
            read(fd, &buf, 1);

            if (buf == 0x5c)
            {
                break;
            }

            if (buf != 0x5d)
            {
                buffer1[i_b1] = buf;
                Bcc_teste ^= buffer1[i_b1];
                i_b1++;
            }
            else
            {
                read(fd, &buf, 1);
                buffer1[i_b1] = buf ^ 0x20;
                Bcc_teste ^= buffer1[i_b1];
                i_b1++;
            }
        }

        if (Bcc_teste == 0x00 && a < NER)
        {
            printf("Bcc verificou\nA enviar RR(%d)...\n", prev_S);
            usleep(distancia_km * 5);
            write(fd, RR[prev_S], 5);
            break;
        }
        else
        {
            a = 1;
            printf("Bcc não verificou\nA enviar REJ(%d)...\n", wrong_S);
            usleep(distancia_km * 5);
            write(fd, REJ[wrong_S], 5);
            Bcc_teste = 0x00;
            i_b1 = 0;
            continue;
        }
    }

    nbytes = i_b1 - 1;
    prev_S = cur_S;
    memcpy(packet, buffer1, nbytes);

    a++;
    // usleep(distancia_km); // distancia

    return nbytes;
}

int llclose(linkLayer connectionParameters, int showStatistics)
{
    gettimeofday(&fim, NULL);
    show = showStatistics;
    if (connectionParameters.role)
    {
        unsigned char buf;

        printf("\nEspera por DISC...\n");

        while (current_state != Stop)
        {
            read(fd, &buf, 1);
            maquina(buf, DISC);
        }

        printf("DISC recebido! A enviar DISC...\n");

        current_state = Start;
        write(fd, DISC, 5);

        printf("DISC enviado! Espera por UA...\n");

        while (current_state != Stop)
        {
            read(fd, &buf, 1);
            maquina(buf, UA);
        }

        current_state = Start;

        printf("UA recebido!\n A fechar conexão\n");
        if (show)
        {
            t_transf = (fim.tv_sec - inicio.tv_sec) * 1000;
            t_transf += (fim.tv_usec - inicio.tv_usec) / 1000;
            printf("Transferência levou %.2f milissegundos\n\n", t_transf);
        }
        close(fd);
        exit(-1);
    }
    else
    {
        char buf;

        flag = clo;
        length_escreve = 5;
        escreve();

        while (current_state != Stop)
        {
            read(fd, &buf, 1);
            maquina(buf, DISC);
        }
        alarm(0);
        current_state = Start;

        printf("DISC recebido!\n A enviar UA e fechar conexão\n");

        write(fd, UA, 5);
        close(fd);
        exit(-1);
    }
    return 1;
}

#endif
