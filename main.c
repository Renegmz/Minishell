/*
 * File:   main.c
 * Author: renegomez
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>

enum { FALSE, TRUE };

//Constantes
#define BUFFER 1024
#define SIZECOMMAND 20
#define SIZEHISTORY 20

void iniciar_minishell()
{
    int minishell_pgid;
    int minishell_terminal;
    int minishell_es_interactiva;
    struct termios minishell_tmodes;

    /*Ver si estamos funcionando de forma interactiva*/
    minishell_terminal = STDIN_FILENO;                     //STDIN_FILENO: es el número de descriptor de archivo de entrada estándar predeterminado(0)
    minishell_es_interactiva = isatty(minishell_terminal); //isatty(): Devuelve 1 si el fd – (descriptor de archivo) se refiere a un terminal.

    if (minishell_es_interactiva)
    {
        /* Ciclo hasta estar en primer plano.  */
        while (tcgetpgrp(minishell_terminal) != (minishell_pgid = getpgrp()))
            kill(-minishell_pgid, SIGTTIN);
        /*
        	tcgetpgrp(): Devuelve el identificador de grupo de procesos, del grupo de procesos en primer plano
        				en la terminal asociada a 'minishell_terminal' (File descriptor fd). que debe ser la terminal
        				de control del proceso invcador.
        	getpgrp(): Devuelve el ID del grupo de proceso actual.
        	kill(): enviará la señal especificada al proceso.
        	SIGTTIN: Señal. Proceso en segundo plano intentatndo leer.
        */

        /*Ignora las señales interactivas y de control de trabajos. */
        signal(SIGINT, SIG_IGN);  //SIGINT: se la manda el terminal al proceso que estemos ejecutando en PP. Por defecto, mata el proceso.
        signal(SIGQUIT, SIG_IGN); //SIGQUIT:
        signal(SIGTSTP, SIG_IGN); //SIGTSTP:
        signal(SIGTTIN, SIG_IGN); //SIGTTIN:
        signal(SIGTTOU, SIG_IGN); //SIGTTOU:
        signal(SIGCHLD, SIG_IGN); //SIGCHLD:

        /*Ponernos en nuestro propio grupo de procesos.  */
        minishell_pgid = getpid();

        /*setpgid(pid_t pid, pid_t pgid): Coloca el ID del grupo del proceso especificado por pid a pgid.*/
        if (setpgid(minishell_pgid, minishell_pgid) < 0)
        {
            perror("No se pudo poner el shell en su propio grupo de procesos");
            exit(1);
        }

        /* Toma el control de la terminal.*/
        tcsetpgrp(minishell_terminal, minishell_pgid);

        /* Guarda los atributos de terminal predeterminados para el shell.  */
        tcgetattr(minishell_terminal, &minishell_tmodes);
    }
}

void leer_comando(char comando[], char *parametros[], char historial[][BUFFER], int cant_comandos)
{
    char buffer[BUFFER];
    char separador[] = " \n";
    char *token;
    char *tokens[SIZECOMMAND];
    int indice = 0;

    //Leyendo la entrada del ususario.
    if (!fgets(buffer, BUFFER, stdin))
        exit(0);

    //Guardando el comando con sus parámetros en el historial
    strncpy(historial[cant_comandos], buffer, strlen(buffer));

    //Separando el comando de sus parámetros
    token = strtok(buffer, separador);
    while (token)
    {
        tokens[indice++] = token;
        token = strtok(NULL, separador);
    }

    //Obteniendo el comando (primer elmento en el array: tokens)
    strcpy(comando, tokens[0]);

    //Obteniendo los parámetros del comando(si los hay).
    for (int i = 0; i < indice - 1; i++)
        parametros[i] = tokens[i + 1];
    parametros[indice - 1] = NULL;

    //return indice;
}

int comando_valido(char comando[])
{
    char *lista_comandos[] = {"movetodir", "whereami", "history", "byebye", "start",
                              "background", "exterminate", "exterminateall", "micd", "umask", "micp"};

    int tam_lista_comandos = sizeof(lista_comandos) / sizeof(char *);
    int es_valido = -1;

    //Buscando el comando ingresado
    for (int i = 0; i < tam_lista_comandos; i++)
    {
        if (strcmp(comando, lista_comandos[i]) == 0)
            es_valido = i;
    }
    return es_valido;
}

//COMANDOS

int movetodir(char *directorio)
{
    int error;

    error = chdir(directorio);
    if (error)
        printf("Error. Movetodir. El directorio [%s]: %s\n", directorio, strerror(errno));
    return error;
}

void whereami()
{
    char directorio[BUFFER];

    if (getcwd(directorio, BUFFER))
        printf("%s\n", directorio);
    else
        printf("Error. Wereami. %s\n", strerror(errno));
}

void history(char *parametros[], char historial[][BUFFER], int cant_comandos)
{
    if (!parametros[0]) //Sin parámetros
        printf("%s", historial[cant_comandos - 2]);
    else
    {
        if (strstr(parametros[0], "-c"))
        {
            for (int i = 0; i < cant_comandos; i++)
                printf("[%d]: %s", i + 1, historial[i]);
        }
        else
            printf("Parametro [%s] Invalido\n", parametros[0]);
    }
}

void start_program(char *parametros[], int background, int programas_iniciados[], int indice)
{
    int pidC, status, pgid;
    char *argv2[4];
    char path[BUFFER];

    pidC = fork();
    if (pidC < 0)
    {
        printf("%s\n", strerror(errno));
        exit(1);
    }

    if (pidC > 0) // Proceso Padre
    {
        if (!background)
            wait(&status); //Espera que el proceso hijo termine
        else
        {
            programas_iniciados[indice] = pidC;
            waitpid(0, &status, WNOHANG); // El padre no se detiene mientras el proceso Hijo (cualquiera) está activo.
            sleep(2);
        }
    }
    else // Proceso Hijo
    {
        pgid = pidC;
        setpgid(pidC, pgid);

        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);

        //Vector de argumentos. Como Máximo 2 argumentos.
        if (parametros[0]) //Nombre del programa
        {
            argv2[0] = parametros[0];

            //¿Tiene parametros?
            if (parametros[1] || parametros[2])
            {
                if (parametros[1] && parametros[2]) //Programa con 2 parametros
                {
                    argv2[1] = parametros[1];
                    argv2[2] = parametros[2];
                    argv2[3] = NULL;
                }
                else //Programa con 1 parametro
                {
                    argv2[1] = parametros[1];
                    argv2[2] = NULL;
                }
            }
            else //Programa sin parametros
                argv2[1] = NULL;

            //PATH ¿Absoluto o Relativo?
            if (strchr(argv2[0], '/')) // Path absoluto
                strcpy(path, "/usr/bin");
            else // Path relativo
            {
                getcwd(path, BUFFER - 1);
                strcat(path, "/");
            }

            if (background)
            {
                printf("%d\n", getpid());
                programas_iniciados[indice] = pidC;
            }

            //Ejecutando el Programa
            strcat(path, argv2[0]);
            if (execvp(path, argv2) < 0)
            {
                printf("Error. StartProgram. Programa [%s]. %s.\n", argv2[0], strerror(errno));
                exit(1);
            }
        }
        else
            printf("Error. No se especifico un nombre de Programa a ejecutar.\n");
    }
}

int exterminate(int pid)
{
    int error;

    if ((error = kill(pid, SIGKILL)) == -1)
        printf("Error: Exterminate. %s\n", strerror(errno));

    return (error);
}

void exterminateall(int programas_iniciados[SIZECOMMAND], int indice)
{
    int procesos_terminados = 0;

    for (int i = 0; i < indice; i++)
    {
        if (exterminate(programas_iniciados[i]) == 0)
            procesos_terminados++;
    }

    printf("%d procesos finalizados ", procesos_terminados);
    for (int i = 0; i < procesos_terminados; i++)
        printf("%d\t", programas_iniciados[i]);
    printf("\n");
}

void micd(char *directorio)
{
    int error;

    if (!directorio) //No tiene parámetros
        error = movetodir(getenv("HOME"));
    else
        error = movetodir(directorio);
    if (!error)
        whereami();
}

/*void my_umask(int mascara, char* parametros[])
{
	printf("Mascara: %d\n", mascara);

	//No se ingresa nuevo valor para la mascara.
	if(!parametros[0])
		//022 valor predeterminado típico para el proceso umask()
		printf("%o\n",umask(022));
	else //Asignando nuevo valor.
	{
		//Se asume que el valor viene dado en formato octal.
		//printf("%o\n",umask(mascara));
	}
}*/

void micp(char *parametros[])
{
    int fd_in, fd_out;
    struct stat statbuf;

    //Abrir archivo de entrada(Origen).
    if ((fd_in = open(parametros[0], O_RDONLY)) == -1)
    {
        printf("Error. Archivo [%s]. %s\n", parametros[0], strerror(errno));
        exit(1);
    }
    fstat(fd_in, &statbuf);

    //Apertura del archivo de salida. Se crea de ser necesario.
    if ((fd_out = open(parametros[1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) == -1)
    {
        printf("Error. Archivo [%s]. %s\n", parametros[1], strerror(errno));
        close(fd_in);
        exit(1);
    }

    //Copiando archivos
    sendfile(fd_out, fd_in, 0, statbuf.st_size);

    if (close(fd_in) == -1 || close(fd_out) == -1)
        printf("Error Cierre de archivo. %s\n", strerror(errno));
}

void ejecutar_comando(char *parametros[], int id_comando, char historial[][BUFFER], int cant_comandos, int programas_iniciados[], int indice)
{
    switch (id_comando)
    {
    case 0: //Comando: movetodir
        movetodir(parametros[0]);
        break;
    case 1: //Comando: whereami
        whereami();
        break;
    case 2: //Comando: history (Con y Sin parámetros)
        history(parametros, historial, cant_comandos);
        break;
    case 3: //Comando: byebye
        exit(0);
        break;
    case 4: //Comando: start
        start_program(parametros, FALSE, programas_iniciados, 0);
        break;
    case 5: //Comando: background
        start_program(parametros, TRUE, programas_iniciados, indice);
        break;
    case 6: //Comando: exterminate
        exterminate(atoi(parametros[0]));
        break;
    case 7: //Comando: exterminateall
        exterminateall(programas_iniciados, indice);
        break;
    case 8: //Comando: micd
        micd(parametros[0]);
        break;
    case 9: //Comando: umask
    //my_umask(atoi(parametros[0]),parametros);
    case 10: //Comando: micp
        micp(parametros);
        break;
    default:
        break;
    }
}

int main(int argc, char const *argv[])
{
    int programas_iniciados[SIZECOMMAND], indice = 0;
    char comando[SIZECOMMAND];
    char *parametros[BUFFER];
    char historial[SIZEHISTORY][BUFFER];
    int id_comando, cant_comandos = 0;

    //Iniciar minishel: Colocar el minishel en primer plano.
    //iniciar_minishell();

    system("clear");
    while (1)
    {
        //Paso 1: Mostrar prompt
        printf("# ");

        //Paso2: Leer Comando.
        leer_comando(comando, parametros, historial, cant_comandos++);

        //Paso 2.1 : Validar Comando
        id_comando = comando_valido(comando);
        if (id_comando >= 0) //Comando Valido(Encontrado).
            //Paso 3: Ejecutar comando
            ejecutar_comando(parametros, id_comando, historial, cant_comandos, programas_iniciados, indice++);
        else
            printf("Error. Comando [%s] No Valido.\n", comando);
    }
    return 0;
}
