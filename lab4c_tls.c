#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <mraa/aio.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

//global variables used by multiple functions
char *file = NULL;
int period = 1;
int fd = -1;
int shuttdown = 0;
mraa_aio_context tempSensor;
char scale = 'F';
int stopReports = 0;
int buffercount = 0;
int cur = 0;
int sock_num = 0;
SSL* ssl;
char ssl_buffer[2048];

//wrapper exit function to deallocate memory
void EXIT(int n){
	mraa_aio_close(tempSensor);
	SSL_shutdown(ssl);
	exit(n);
}

//function that reads the sesnors temperature
float readTemp()
{
	int B = 4275;               // B value of the thermistor
	int R0 = 100000;
    	float input = mraa_aio_read(tempSensor);;
        float R = 1023.0/input-1.0;
	R = R0*R;
	float temperature = 1.0/(log(R/R0)/B+1/298.15)-273.15; // convert to temperature via datasheet
	if (scale == 'F')
		return temperature * 9/5 + 32;
	else
		return temperature;
}

//logs shutdown and ends the program
void end_program(char * output_time){
	sprintf(ssl_buffer, "%s SHUTDOWN\n", output_time);
	SSL_write(ssl, ssl_buffer, strlen(ssl_buffer));
	dprintf(fd, "%s SHUTDOWN\n", output_time);
	EXIT(0);
}

//parses stdin through a circular buffer
void readInput(char* inputBuffer, int r){
	int end = r + buffercount; //buffercount keeps track of where new data is read into buffer, r is number of bytes read
	while (buffercount < end){
		if (inputBuffer[buffercount] == '\n'){ //newline means that there is meaningful input to parse
		int diff = buffercount - cur; //ensures that out of bounds memory is not accessed
			if (diff >= 7 && inputBuffer[cur] == 'S' && inputBuffer[cur+1] == 'C' && inputBuffer[cur+2] == 'A' && inputBuffer[cur+3] == 'L' && inputBuffer[cur+4] == 'E' && inputBuffer[cur+5] == '='){ //scale options
				if (inputBuffer[cur+6] == 'F'){
					scale = 'F';
					dprintf(fd, "%s\n", strtok(inputBuffer+cur,"\n")); //logs command
				}
				else if (inputBuffer[cur+6] == 'C'){
					scale = 'C';
					dprintf(fd, "%s\n", strtok(inputBuffer+cur,"\n"));
				}
				else{
					fprintf(stderr, "Error: Incorrect scale option\n");
				}
				cur = buffercount+1; //update current position in circular buffer
			}
			else if (diff >= 7 && inputBuffer[cur] == 'P' && inputBuffer[cur+1] == 'E' && inputBuffer[cur+2] == 'R' && inputBuffer[cur+3] == 'I' && inputBuffer[cur+4] == 'O' && inputBuffer[cur+5] == 'D' &&inputBuffer[cur+6] == '=') { //period options
				period = atoi(strtok(inputBuffer + cur + 7, "\n"));
				if (period < 1){
					fprintf(stderr, "Error: period cannot be less than 1.\n");
					period=1;
				}
				dprintf(fd, "%s\n", strtok(inputBuffer+cur,"\n"));
				cur = buffercount + 1;
			}
			else if (diff >= 4 && inputBuffer[cur] == 'S' && inputBuffer[cur+1] == 'T'){ // stop and start commands
				if (inputBuffer[cur+2] == 'O' && inputBuffer[cur+3] == 'P'){
					stopReports = 1; //global variables used to stop reporting
				}
				else if (diff >= 5 && inputBuffer[cur+2] == 'A' && inputBuffer[cur+3] == 'R' && inputBuffer[cur+4] == 'T')
					stopReports = 0;
				dprintf(fd, "%s\n", strtok(inputBuffer+cur, "\n"));
				cur = buffercount + 1;
			}
			else if (diff >= 3 && inputBuffer[cur] == 'L' && inputBuffer[cur+1] == 'O' && inputBuffer[cur+2] == 'G'){ //log command
				dprintf(fd, "%s\n", strtok(inputBuffer+cur, "\n"));
				cur = buffercount+1;
			}
			else if (diff >= 3 && inputBuffer[cur] == 'O' && inputBuffer[cur+1] == 'F' && inputBuffer[cur+2] == 'F'){ //off command
				shuttdown = 1;
				dprintf(fd, "%s\n", strtok(inputBuffer+cur, "\n"));
				cur = buffercount+1;
			}
			else {
				fprintf(stderr, "unrecognized command\n");
				cur = buffercount+1;
			}
			if (buffercount == end-1){
				buffercount = 0;
				cur = 0;
				break;
			}
		}
	buffercount += 1;
	}
	if (buffercount >= 2048){ //reset buffer
		buffercount = 0;
		cur = 0;
	}
}

int main(int argc, char** argv) {	
	char* host = NULL;
        int id = 0;
        char* file = NULL;
        struct option opts[] = {
        {"scale", required_argument, NULL, 's'},
        {"period", required_argument, NULL, 'p'},
        {"log", required_argument, NULL, 'l'},
        {"host", required_argument, NULL, 'h'},
        {"id", required_argument, NULL, 'i'},
        {0, 0, 0, 0}
        };	

	int optresult;
	//parses the options
	while ((optresult = getopt_long(argc, argv, "", opts, NULL)) != -1){
	switch (optresult){
		case 'l':
			file = optarg;
			fd = creat(file, 00666);
			if (fd < 0){
				fprintf(stderr, "Error: unable to open log file specified by --log command. File is %s. Error value is %s.\n", file, strerror(errno));
				exit(1);
			}
			break;
		case 's':
			scale = optarg[0];
			if (scale != 'F' && scale != 'C'){
				fprintf(stderr, "Error: invalid scale option. Must be 'F' or 'C'\n");
				exit(1);
			}
			break;
		case 'p':
			period = atoi(optarg);
			if (period < 1){
				fprintf(stderr, "Error: period must be greater than 1\n");
				exit(1);
			}
			break;
		case 'h':
			host = optarg;
			break;
		case 'i':
			id = atoi(optarg);
			break;
		case 0:
			break;
		case '?':
		default:
		fprintf(stderr, "Error: correct usage is ./lab4b_tls [--period=<number>] [--scale=<number>] [--log=<filename>] [--host=<hostname>] [--id=<user_id>]\n");
		exit(1);
	}
	}
	
	//check for required arguments
	int port = atoi(argv[argc - 1]);
        if (port == 0) {
                fprintf(stderr, "Error: port number must be specified.\n");
                exit(1);
        }
        if (!id){
                fprintf(stderr, "Error: user id must be specified.\n");
                exit(1);
        }
        if (!host){
                fprintf(stderr, "Error: host must be specified.\n");
                exit(1);
        }
        if (fd == -1){
                fprintf(stderr, "Error: log file must be specified.\n");
                exit(1);
        }

        //create socket
        sock_num = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_num == -1){
                fprintf(stderr, "System call socket failed. Error value is: %s\n", strerror(errno));
                exit(1);
        }

	//check for host server
        struct sockaddr_in server;
        struct hostent * host_ip = gethostbyname(host);
        if (host_ip == NULL){
                fprintf(stderr, "Error: host does not exist.\n");
                exit(1);
        }

        memset((char *) &server, 0, sizeof(server));
        memcpy((char *) &server.sin_addr.s_addr, (char *) host_ip->h_addr, host_ip->h_length);
        server.sin_family = AF_INET;
        server.sin_port = htons(port);

	//data structures used for polling
	struct pollfd pollInput;
	pollInput.fd = sock_num;
	pollInput.events = POLLIN;

	//data structures used to keep track of time
	time_t seconds1;
	time_t seconds2 = 0;
	struct tm *current_time;
	char output_time[10];
	float currentTemp = 0;
	char inputBuffer[2048];
	
	//intialize ssl structures
	if (SSL_library_init() < 0){
		fprintf(stderr, "SSL initialization failed with message %s.\n", strerror(errno));
		exit(2);
	}

	//add ssl functionality
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();
	
	//set up ssl context
	SSL_CTX* ssl_ctx;	
	const SSL_METHOD* ssl_method;
	ssl_method = SSLv23_client_method();
	if((ssl_ctx = SSL_CTX_new(ssl_method)) == NULL) {
		fprintf(stderr, "Error: unable to create new ssl context. %s\n", strerror(errno));
		exit(2);
	}
	ssl = SSL_new(ssl_ctx);
	
	//initialize sensors
	tempSensor = mraa_aio_init(1);

	//connect to the server
	if (connect(sock_num, (struct sockaddr *)&server , sizeof(server)) == -1){
                fprintf(stderr, "System call connect failed. Error value is: %s\n", strerror(errno));
                EXIT(2);
        }
	
	//finish ssl connections
	if (SSL_set_fd(ssl, sock_num) == 0){
		fprintf(stderr, "Error: ssl file descriptor facility failed. %s\n", strerror(errno));
		EXIT(2);
	}
	if(SSL_connect(ssl) == 0) {
		 fprintf(stderr, "Error: ssl connection failed. %s\n", strerror(errno));
		 EXIT(2);
	}

	//log id
        sprintf(ssl_buffer, "ID=%d\n", id);
        SSL_write(ssl, ssl_buffer, strlen(ssl_buffer));
	dprintf(fd, "ID=%d\n", id);
	
	//loop until program is shutdown
	while(1){
		time(&seconds1); //get the current time
		if (seconds1 - seconds2 >= period) { //create a report if specified period of time has passed
			if (!stopReports){ //logs a report from current time
				seconds2 = seconds1; //updates time counter
				current_time = localtime(&seconds1);
				strftime(output_time, 10, "%H:%M:%S", current_time);
				currentTemp = readTemp(); //gets temp reading
				if (!shuttdown){
					sprintf(ssl_buffer, "%s %.1f\n", output_time, currentTemp);
					SSL_write(ssl, ssl_buffer, strlen(ssl_buffer));
					dprintf(fd, "%s %.1f\n", output_time, currentTemp); 
				}
				else { 
					end_program(output_time);
				}
			}
		}
		
		if (shuttdown){ //creates shutdown report if shutdown sequence is signaled
			current_time = localtime(&seconds1);
			strftime(output_time, 10, "%H:%M:%S", current_time);
			end_program(output_time);
		}
		
		if (poll(&pollInput, 1, 0) == -1){ //polls for input from stdin
			fprintf(stderr, "Error: poll system call failed. Error message is: %s", strerror(errno));
			EXIT(2);
		}

		if (pollInput.revents & POLLIN){ //if input is received, parse input
			int r;
			if ((r = SSL_read(ssl, inputBuffer+buffercount, 64)) < 0){ //read input
				fprintf(stderr, "Error: Read system call failed. Error message is: %s", strerror(errno));
				EXIT(2);
			}
			if (r == 0){ //if pipe is closed, end program
				current_time = localtime(&seconds1);
				strftime(output_time, 10, "%H:%M:%S", current_time);
				end_program(output_time);
			}
			readInput(inputBuffer, r); //parse input function is called
		}
	}
	EXIT(0);
}
