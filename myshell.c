#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "parser.h"

/* declaración de funciones */
void prompt();

int main(void) {

	/* pid del proceso padre */
	pid_t pid;
	/* buffer que guarda los datos introducidos por linea de comandos */
	char buf[1024];
	/* string para guardar el directorio al usar cd */
	char * directorio;
	/* variable que organiza la informacion contenida en el buffer */
	tline * line;
	/* declaración de un array de pipes */
	int **pipe_des;
	/* variables para guardar los descriptores de fichero */
	int fdentrada, fdsalida, fderror;
	/* variables que nos sirven de contador */
	int i,j,k;
	/* variable para control de errores y ejecución */
	int no_exec;

	/* el programa ignora las señales SIGINT Y SIGQUIT */
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT, SIG_IGN);

	prompt();

	while (fgets(buf,1024,stdin)) { /* mientras se escriba texto por la linea de comandos */

		if (strcmp(buf, "\n") != 0) { /* si la línea no está vacía (si no es un salto de línea) */

			line = tokenize(buf); /* organizamos la informacion obtenida en la variable line */

			if (strcmp(line->commands[0].argv[0],"cd") == 0) { /* si llamamos al mandato cd */

				if (line->ncommands > 1) { /* comprobamos que no se ejecute con pipes, si es así */
					fprintf(stderr,"El mandato cd no se puede ejecutar con pipes\n");
				} else { /* si se ejecuta en solitario sin pipes */

					if(line->commands[0].argc > 2) { /* controlamos que no tenga más de un argumento, si lo tiene */
						fprintf(stderr,"Este mandato puede tener como máximo un argumento\n");
					} else { /* si tiene el número correcto de argumentos */

						if (line->commands[0].argc == 1) { /* si no tiene argumento, cambiamos de directorio a HOME */
							directorio = getenv("HOME");
							if (directorio == NULL) { /* y la variable HOME si no existe, se indica el error */
								fprintf(stderr,"No existe la variable $HOME\n");
							}
						} else { /* si tiene un argumento, se cambia de directorio a este */
							directorio = line->commands[0].argv[1];
						}

						if (chdir(directorio) != 0) { /* por ultimo comprobamos si el argumento es un directorio, si no lo es */
							fprintf(stderr,"No se pudo cambiar el directorio. Error: %s\n", strerror(errno));
						} else { /* si es un directorio */
							printf("Se cambio el directorio a: %s\n", getcwd(buf,-1));
						}

					} /* fin de cd con numero correcto de argumentos */

				} /* fin de cd sin pipes */

			} /* fin de llamada al mandato cd */
			else { /* si llamamos a un mandato distinto a cd */

				no_exec = 0;
				for (k = 0; k < line->ncommands; k++) { /* recorremos la línea para buscar errores */
	  				if (line->commands[k].filename == NULL && strcmp(line->commands[k].argv[0],"cd") != 0) { /* si algún mandato no existe */
	  					fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[k].argv[0]);
	  					no_exec++;
	  				}
	  				if (strcmp(line->commands[k].argv[0],"cd") == 0) { /* si se quiere ejecutar cd con con pipes */
	  					fprintf(stderr,"El mandato cd no se puede ejecutar con pipes\n");
	  					no_exec++;
	  				}
	  			} /* fin de recorrer la línea para buscar errores */

				if (no_exec < 1) { /* si todos los mandatos existen y no se usan pipes con cd */
					/* reservamos memoria dinamica para el array de pipes */
					pipe_des = (int **) malloc ((line->ncommands - 1) * sizeof (int *));
					for(i = 0; i < (line->ncommands - 1); i++) {
						pipe_des[i] = (int *) malloc( 2 * sizeof(int));
					}

					for (k = 0; k < line->ncommands; k++) { /* recorremos los mandatos de la línea */
						if (k != line->ncommands - 1) { /* para los mandatos excepto el último */
							pipe(pipe_des[k]); /* creamos un pipe */
						}

						pid = fork(); /* llamamos a fork() para crear procesos hijo */

						if (pid < 0) { /* controlamos que el fork haya ido mal */
							fprintf(stderr,"Fallo en el fork.");
							exit(1);
						}

						if (pid == 0) { /* si es hijo */
							/* los hijos son sensibles a las señales SIGINT y SIQUIT */
							signal(SIGQUIT, SIG_DFL);
							signal(SIGINT, SIG_DFL);

							if (k == (line->ncommands - 1)) { /* si es el último mandato */
								if (line->redirect_output != NULL) { /* si hay redirección de salida */
									fdsalida = open (line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666); /* guardamos en 'fdsalida' el fichero de salida */
									dup2 (fdsalida,1); /* hacemos que la salida sea a traves del fichero */
									if (fdsalida == -1) { /* si hay algún error abriendo el fichero */
										fprintf(stderr,"%s: Error. %s\n",line->redirect_output, strerror(errno));
										exit(1); /* termina la iteración */
									}
								}
								if (line->redirect_error != NULL) { /* si hay redirección de salida de error */
									fderror = open (line->redirect_error, O_WRONLY | O_CREAT | O_TRUNC, 0666); /* guardamos en 'fderror' el fichero de salida de error */
									dup2 (fderror,2); /* hacemos que la salida de error sea a traves del fichero */
									if (fderror == -1) { /* si hay algún error abriendo el fichero */
										fprintf(stderr,"%s: Error. %s\n",line->redirect_error, strerror(errno));
										exit(1); /* termina la iteración */
									}
								}
							}
							if (k == 0 && line->redirect_input != NULL) { /* si es el primer mandato y hay redirección de entrada */
								fdentrada = open (line->redirect_input, O_RDONLY); /* guardamos en 'fdentrada' el descriptor del fichero de entrada */
								if (fdentrada == -1) { /* si hay algún error abriendo el fichero */
									fprintf(stderr,"%s: Error. %s\n",line->redirect_input, strerror(errno));
									 exit(1); /* termina la iteración */
								} else {
									dup2(fdentrada,0); /* hacemos que la entrada sea a traves del fichero */
								}
							}
							/* para todos los procesos intermedios redirigimos la salida a traves del pipe y cerramos lo que no usa */
							if (k != (line->ncommands - 1)) {
								dup2(pipe_des[k][1],1);
								close(pipe_des[k][0]);
							}
							/* para todos los procesos intermedios redirigimos la entrada a traves del pipe y cerramos lo que no usa */
							if (k != 0) {
								dup2(pipe_des[k-1][0],0);
								close(pipe_des[k-1][1]);
							}
							execvp(line->commands[k].filename, line->commands[k].argv); /* ejecutamos el correspondiente mandato */
							fprintf(stderr,"Error de ejecucución"); /* si llegamos aqui, hay un error */
							exit(1);

						} /* fin de hijo */
						else { /* si es padre */

							/* cerramos los pipes usados */
							if (k != 0) {
								close(pipe_des[k-1][0]);
								close(pipe_des[k-1][1]);
							}
							wait(NULL); /* espera a los hijos */

						} /* fin de padre */

					} /* fin de recorrer los mandatos de la línea */

					/* liberamos la memoria de los pipes */
					for(j = 0; j < (line->ncommands - 1); j++) {
						free(pipe_des[j]);
					}
					free(pipe_des);

				} /* fin de mandatos que no son cd invocados correctamente */

			} /* fin de llamada a mandato distinto a cd */

		} /* fin de si la linea no está vacía */

		prompt();

	} /* fin del while */

	return 0;

} /* fin del main */

/* función que imprime el prompt por pantalla */
void prompt() {
	printf("msh> ");
}

