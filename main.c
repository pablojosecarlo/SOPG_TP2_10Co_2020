/*
 * SOPG TP2 10Co 2020
 * Pablo J.C. Alonsp Castillo
 * 19/IV/2020
 * 
 * Serial service - tres luces
 * 
 * control de apertura de puerto tcp
 * control de reconexión del cliente tcp
 * control de broken pipe 
 * 
 * sigint sigterm - cierre controlado de los threads
 * sigpipe - finalmente no se usó.
 * 
 * Si no hay conecciones activas del server, el puerto 
 * serie hace un loop cerrado y devuelve las >SW:x,y como >COM:x,y
 * de modo que las luces se controlan desde la misma botonera
 * 
 * implementado en 5 threads. . 
 * 
 * NOTA: El Main.py se manda de las suyas . . . .  
 * Muchas veces crei que era mi programa, pero era el cliente
 * TCP el que decia una cosa y hacia otra, sobre todo cuando se 
 * lo abusa un poco con el ctrl+C para probar la resiliencia 
 * de nuestro soft. Esto me hizo pasar muchos sustos!! 
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#include "SerialManager.h"

#define BUFFER_SIZE 128
#define FALSE 0
#define TRUE  1
#define OFF   0
#define ON    1

typedef int  bool_t;

//Funciones asociadas al puerto serie
void* thread_SerialReceive	   ( void* );
void* thread_SerialSend    	   ( void* );

//Funciones asociadas al servidor TCP
void* thread_ServerTcp 		   ( void* );
void* thread_ClienteTcpReceive ( void* );
void* thread_ClienteTcpSend    ( void* );

//Funciones asociadas al manejo de los leds
void  LedOnOffSerial ( int, bool_t );
void  LedOnOffServer ( int, bool_t );

//funciones de testing de los leds
void  LedsOff        ( void );
void  LedsOn         ( void );

//Funciones asociadas a las signals del SO
void  desbloquearSignals     ( void );
void  bloquearSignals        ( void );
void  sigint_handler         ( int  );
void  sigterm_handler        ( int  );
void  sigpipe_handler        ( int  );
void  asignarSignalsHandlers ( void );

//señal de terminación (sigint - sigterm)
bool_t sgn_Terminar= FALSE;

//Globales para la comunicación con los threads
char inBuffSerial [ BUFFER_SIZE ]; 
char outBuffSerial[ BUFFER_SIZE ];
char inBuffServer [ BUFFER_SIZE ]; 
char outBuffServer[ BUFFER_SIZE ];

//Socket File descriptors
int srvSckFd;
int newSckFd;
int oldSckFd;

//los threads de puerto serie y server
pthread_t tsR, tsS, tSv;
//las señales para los threads
bool_t  sgn_SerialReceive_OK = FALSE;
bool_t  sgn_SerialSend_OK	 = FALSE;

//número de conecciones activas del sever
int nCnxServer = 0; 

//los threads del cliente del server
pthread_t tcR, tcS;
//las señales para los threads
bool_t  sgn_TcpReceive_OK = FALSE;
bool_t  sgn_TcpSend_OK    = FALSE;

//señal de Broken pipe - cliente cortado
bool_t  sgn_TcpBrokenPipe = FALSE;

int bytesRead   = 0;
int bytesToSend = 0;

//solo un mutex
pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER;

int main(void)
{
	printf("\e[1;1H\e[2J"); //limpio la terminal
	printf("pid main: %d\n", getpid()); //pongo el pid del main

	//declaraciones de variables
		int puertoNum = 1;
		int puertoVel = 115200;

    //variables auxiliares para manejo de strings
		int Led = 0;
		int OnOff = 0;

	asignarSignalsHandlers();
	bloquearSignals();

	//inicio puerto serie
		printf( "Inicio Serial Service\r\n" );
		if( serial_open( puertoNum, puertoVel ) ){
			perror( "serial_open_error" );
			exit(1);
		}else{
			printf( "Puerto abierto N°: %d a %d \n", puertoNum, puertoVel );
		}

	//inicio threads Ok => retorna 0

		if( pthread_create (&tsR, NULL, thread_SerialReceive, NULL)){
			perror( "thread_SerialRreceive_create_error" );
			exit(1);
		};

		if( pthread_create (&tsS, NULL, thread_SerialSend, NULL)){
			perror( "thread_SerialSend_create_error" );
			exit(1);
		};

		if( pthread_create (&tSv, NULL, thread_ServerTcp, NULL)){
			perror( "thread_ServerTcp_create_error" );
			exit(1);
		};
	
	sleep(1);

	desbloquearSignals();
	
	//LedsOff();  //para pruebas. . . 
	//LedsOn;

	//loop principal
	do
	{	
		sleep(0.1);
		if (sgn_SerialReceive_OK){
			pthread_mutex_lock (&mutexData);	

				printf( "thread_SerialReceive: %s\n", inBuffSerial );
				if (inBuffSerial[0]=='>'){
					Led   = inBuffSerial[4] - 48;
					OnOff = inBuffSerial[6] - 48;
					if (nCnxServer)
						LedOnOffServer( Led, OnOff ); //loop serie tcp etc.
					else
						LedOnOffSerial( Led, OnOff ); //lopp serie serie
				}
				
				sgn_SerialReceive_OK = FALSE;
			pthread_mutex_unlock (&mutexData);	
		}

		if (sgn_TcpReceive_OK){
			pthread_mutex_lock (&mutexData);	

				printf( "thread_ClienteTcpReceive: %s\n", inBuffServer );
				if (inBuffServer[0]=='>'){
					Led   = inBuffServer[5] - 48;
					OnOff = inBuffServer[7] - 48;
					LedOnOffSerial( Led, OnOff );
				}

				sgn_TcpReceive_OK = FALSE;
			pthread_mutex_unlock (&mutexData);	
		}

		//Si se desconecta el cliente TCP y me entero por las malas. . . 
		if (sgn_TcpBrokenPipe ){ 

			pthread_mutex_lock (&mutexData);	
				close(oldSckFd);

				pthread_cancel( tcR );
				pthread_cancel( tcS );

				pthread_join( tcR, NULL );
				pthread_join( tcS, NULL );

				nCnxServer = 0;
			pthread_mutex_unlock (&mutexData);	

		}

	}while( ! sgn_Terminar );

//Terminado. . .
	printf ( "Cerrando los threads y demás. . . .\n" );

	pthread_cancel( tsR );
	pthread_cancel( tsS );
	pthread_cancel( tcR );
	pthread_cancel( tcS );
	pthread_cancel( tSv );

	pthread_join( tsR, NULL );
	pthread_join( tsS, NULL );
	pthread_join( tcR, NULL );
	pthread_join( tcS, NULL );
	pthread_join( tSv, NULL );

	serial_close();

	printf ( "FIN. . . .\n" );

	exit( EXIT_SUCCESS );
	
	return 0;
}

void LedOnOffSerial( int Led, bool_t OnOff )
{
	memset ( outBuffSerial, 0, BUFFER_SIZE );
	sprintf( outBuffSerial, ">OUT:%u,%u\r\n", Led, OnOff );
	bytesToSend =  strlen( outBuffSerial );
	
	sgn_SerialSend_OK = TRUE;	
}

void LedOnOffServer( int Led, bool_t OnOff )
{
	memset ( outBuffServer, 0, BUFFER_SIZE );
	sprintf( outBuffServer, ">SW:%u,%u\r\n", Led, OnOff );
	bytesToSend =  strlen( outBuffServer );

	sgn_TcpSend_OK = TRUE;	
}

void* thread_SerialReceive (void* message)
{
	printf( "Ok! SerialReceive\n" );
	do
	{
		sleep(0.1);   
		pthread_mutex_lock (&mutexData);			
			bytesRead = serial_receive( inBuffSerial, BUFFER_SIZE); 
			if ( bytesRead == 9 ) //strlen(">SW:x,y\r\n") = 9
			{
				inBuffSerial[ bytesRead - 2 ] = '\0'; //saco \r\n, agrego \0
				sgn_SerialReceive_OK = TRUE;
			}
		pthread_mutex_unlock (&mutexData);
	} 
	while ( 1 );
	return NULL;
}

void* thread_SerialSend (void* message)
{
	printf( "Ok! SerialSend\n" );
	do
	{
		sleep(0.1);
		if ( sgn_SerialSend_OK )
        {
			pthread_mutex_lock (&mutexData);
				bytesToSend = strlen(outBuffSerial);
				if ( bytesToSend == 10 )//strlen(">OUT:x,y\r\n") = 10
				{
					serial_send( outBuffSerial, bytesToSend ); 
					printf( "thread_SerialSend %u bytes, %s\n", bytesToSend, outBuffSerial );
					sgn_SerialSend_OK = FALSE;
				}
			pthread_mutex_unlock (&mutexData);
        }
	} 
	while ( 1 );
	return NULL;
}

void* thread_ServerTcp (void* message)
{
	printf( "Ok! ServerTcp\n" );

	socklen_t addr_len;
	struct sockaddr_in clientaddr; //es *_in porque son sockets de Internet
	struct sockaddr_in serveraddr;
	int puerto = 10000;
	char ipClient[32];
	int backlog = 1; //= cantidad de pedidos de coneccíón que se acumulan

		printf ("conecciones activas %d\n", nCnxServer);

	// Creamos el socket
	if ( ( srvSckFd = socket(AF_INET,SOCK_STREAM, 0)) == -1){
		fprintf(stderr,"ERROR No pude crear el Socket IP\r\n");
		exit(1);
	} 

	// Cargamos datos de IP:PORT del server
    	bzero((char*) &serveraddr, sizeof(serveraddr)); //carga 0's
    	serveraddr.sin_family = AF_INET;  //indico que es de internet
    	serveraddr.sin_port = htons(puerto);//acomoda en formato de red BIGENDIAN htons =  host to network short
    	//serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");  //solo sirve para ipv4 no se recomienda
    	if(inet_pton(AF_INET, "127.0.0.1", &(serveraddr.sin_addr))<=0)  //esta sirve para todas las familias, pton = presentation to network por ej ipv6
    	{
        	fprintf(stderr,"ERROR invalid server IP\r\n");
        	exit(1);
    	}

	// Abrimos puerto con bind()
	int rBind, cBind; //retorno de bind y contador de bind
	cBind = 12;
	while( ((rBind = bind( srvSckFd, (struct sockaddr*)&serveraddr, sizeof(serveraddr))) == -1) && cBind > 0) {
		printf( "Reintentando bindear, intentos restantes: %d\n", cBind );
		sleep(10);
		cBind--;
	}
	if (rBind < 0 ){  //No hubo caso. . .
		close( srvSckFd );
		perror("listener bind");
		exit(1);
	}

	// Seteamos socket en modo Listening
	if (listen ( srvSckFd, backlog) == -1) // backlog= cantidad de pedidos de coneccíón que se acumulan
  	{
    	perror("error en listen");
    	exit(1);
  	}
	//Ya estamos en condiciones de escuchar conecciones entrantes. . . .

	while(1)  //El accept queda dentro de un bucle para que cuando termine vuelva de nuevo a aceptar otra BLOQUEANTE
	{
		// Ejecutamos accept() para recibir conexiones entrantes
		addr_len = sizeof(struct sockaddr_in);	//esta variable la defino porque accept es rara y quiere
							//un puntero al size del sockaddr y no el size directamente 
							//esto se llama: result argument value. La usa para leer un 
							//valor y para devolver un valor (que deberian ser iguales)
    	if ( (newSckFd = accept( srvSckFd, (struct sockaddr *)&clientaddr, &addr_len)) == -1)
      	{
			perror("error en accept");
			exit(1);
	    }//El código queda bloqueado aqui mientras no haya un bind que se quiera conectar. . . . 
		 //En cuanto se conecta un cliente, accept me deja el nuevo filedescriptor: newSckFd del cliente que se conectó
	 			 
		//Aqui utilizo la estructura &(clientaddr.sin_addr) para leer la dirección del cliente 
		//y guardarla en el string ipClient usando la funcion inet_ntop: network to presentation
		//si estamos en la misma máquina cliente y servidor tendrán la misma ip 127.0.0.1
		inet_ntop(AF_INET, &(clientaddr.sin_addr), ipClient, sizeof(ipClient));
		printf  ("server:  conexion desde:  %s\n",ipClient);

		sleep(0.1);
		nCnxServer++;
		if (nCnxServer == 1) oldSckFd = newSckFd; //copio en oldSckFd para cerrarla cuando aparezca una nueva
		printf ("conecciones activas %d\n", nCnxServer);

		//Si el cliente se cierra y quiere reconectarse. . . . aparecerá una segunda conexión
		//Así que cierro la primer conexión (oldSckFd), cierro los threads y los reconecto con la segunda.
		if (nCnxServer == 2){ 
			
			close(oldSckFd);
			oldSckFd = newSckFd;

			pthread_cancel( tcR );
			pthread_cancel( tcS );

			pthread_join( tcR, NULL );
			pthread_join( tcS, NULL );

			nCnxServer = 1;
		}

		//Lanzo los threads (o los relanzo si hubo reconexión. . . )
		//Atención: Si el cliente se corta antes de lanzarce los threads 
		//la interacción del sistema con el cliente se vuelve catatónica. 
		if (nCnxServer < 2) //solo acepto 1
		{	
			if( pthread_create (&tcR, NULL, thread_ClienteTcpReceive, NULL )){
				perror( "thread_ClienteTcpReceive_create_error" );
				exit(1);
			}

			if( pthread_create (&tcS, NULL, thread_ClienteTcpSend, NULL )){
				perror( "thread_ClienteTcpSend_create_error" );
				exit(1);
			}
		}

		// prueba cierre de conexion. El cliente recibira SIGPIPE.
		/*
		printf("presionar enter para salir\n");
		getchar();
		close(newSckFd);
		close(srvSckFd);
		exit(1);
		*/

	}//vuelvo al accept
	
	return NULL;
}

void* thread_ClienteTcpReceive (void* message)
{
	printf( "Ok! ClienteTcpReceive\n" );
	do  //El read es BLOQUEANTE ha sido instaciado en un thread y liberado del accept
	{	// Leemos mensaje de cliente
		//if( (n = recv(newSckFd,buffer,128,0)) == -1 ) //recv es igual que write pero con unos flags al final
		if( (bytesRead = read(newSckFd, inBuffServer ,128 )) == -1 ) //A partir de ahora el read y el write se hacen con el newSckFd 
		{					     									 //de la conección que se creó con ese cliente particular
			perror("Error leyendo mensaje en socket");
			exit(1);
		}

		pthread_mutex_lock (&mutexData);	

			if ( bytesRead == 10 ) //strlen(">OUT:x,y\r\n") = 10
			{
				inBuffServer[ bytesRead - 2 ] = '\0'; //saco \r\n, agrego \0
				sgn_TcpReceive_OK = TRUE;
			}

		pthread_mutex_unlock (&mutexData);	

	}while(1);

	return NULL;
}

void* thread_ClienteTcpSend (void* message)
{
	printf( "Ok! ClienteTcpSend\n" );
	do
	{
		sleep(0.1);
		if ( sgn_TcpSend_OK );
        {
			pthread_mutex_lock (&mutexData);
				bytesToSend = strlen(outBuffServer);
				if (bytesToSend == 9) //strlen(">SW:x,y\r\n") = 9
				{
					if (write (newSckFd, outBuffServer, bytesToSend ) == -1 ) //podríamos haber usado send con flag 0. . .creo. . 
					{
						perror("Error escribiendo mensaje en socket clienteTcp");
						sgn_TcpBrokenPipe = TRUE; 
						//exit (1);
					}
					if (!sgn_TcpBrokenPipe) 
						printf( "thread_ClienteTcpSend %u bytes, %s", bytesToSend, outBuffServer );
					memset(outBuffServer, 0, bytesToSend);
				}
				sgn_TcpSend_OK = FALSE;
			pthread_mutex_unlock (&mutexData);
		}
		
	}while(1);
	
	return NULL;
}

void bloquearSignals(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGPIPE);
    //sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void desbloquearSignals(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGPIPE);
    //sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

void sigint_handler(int sig)
{
	write(0, "...Ahhh! SIGINT!\n", 17);
	sgn_Terminar= TRUE;
}

void sigterm_handler(int sig)
{
	write(0, "...Ohhh! SIGTERM!\n", 18);
	sgn_Terminar= TRUE;
}

void sigpipe_handler(int sig)
{
	write(0, "...Caramba! SIGPIPE!\n", 21);
	//sgn_Terminar= TRUE;
}

void asignarSignalsHandlers ( void )
{
	struct sigaction sa;

	// signal ctrl+c
	sa.sa_handler = sigint_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		perror("sigaction_sigint");
		exit(1);
	}

	// signal sigterm
	sa.sa_handler = sigterm_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		perror("sigaction_sigterm");
		exit(1);
	}

	// signal sigpipe
	sa.sa_handler = sigpipe_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGPIPE, &sa, NULL) == -1) {
		perror("sigaction_sigpipe");
		exit(1);
	}

}

void LedsOff( void )
{
	LedOnOffSerial( 0, OFF );
	sleep(1);
	LedOnOffSerial( 1, OFF );
	sleep(1);
	LedOnOffSerial( 2, OFF );
}

void LedsOn( void )
{
	LedOnOffSerial( 0, ON );
	sleep(1);
	LedOnOffSerial( 1, ON );
	sleep(1);
	LedOnOffSerial( 2, ON );
}
