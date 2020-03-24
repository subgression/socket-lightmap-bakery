#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ifaddrs.h>

#define LMB_MAX_FILES_CLIENT 100
#define LMB_MAX_FILES_IN_QUEQUE 100
#define LMB_BUFFER_SIZE 2048
#define SA struct sockaddr
#define PORT 8080
#define LMB_BUFFER_SIZE 1024

//Struct containing all the informatiomn of the file to be baked
struct bake_file
{
    char *path;
    char *filename;
    char script[128];
};

enum connection_result
{
    OK,
    ERR_SOCKET_CREATE,
    ERR_INVALID_ADDR
};

enum connection_status
{
    IDLE,
    SEND_FILE,
    RECV_FILE,
};

//All the communication messages needed for the app to work
static char *LMB_REQ_SEND_FILE = "LMB_REQ_SEND_FILE";
static char *LMB_SEND_FILE_OK = "LMB_SEND_FILE_OK";
static char *LMB_SEND_FILE_ERR = "LMB_SEND_FILE_ERR";
static char *LMB_ACK_SEND_FILE = "LMB_ACK_SEND_FILE";
static char *LMB_TEST_MSG = "LMB_TEST_MSG";
static char *LMB_TEST_MSG_OK = "LMB_TEST_MSG_OK";
static char *LMB_FILE_ENDED = "LMB_FILE_ENDED";
static char *LMB_FILE_ENDED_ACK = "LMB_FILE_ENDED_ACK";
static char *LMB_NO_PROTO_MSG = "LMB_NO_PROTO_MSG";
static char *LMB_STD_ACK = "LMB_STD_ACK";

//STATIC path for the blender app
//It will work only on MacOS devices with Blender 2.78 (Might work with 2.8 since they have the same path)
//[TODO] This will change using just 'blender' for executing the script, didn't have time for that atm
static char *BLENDER_FILE_PATH = "/Applications/Blender/blender.app/Contents/MacOS/blender";

// Send a file to the given socket
// sock: The socket to send the file
// filename: The filename to send to the socket
void lmb_send_file(int sock, char *filename)
{
    FILE *fd;
    double total_read = 0;
    double all_read = 0;
    char buffer[LMB_BUFFER_SIZE];

    //Opening file
    fd = fopen(filename, "rb");
    if (fd == NULL)
    {
        printf("Error Opening File\n");
        return;
    }

    //Repeat until read at least 1 byte in the buffer
    while ((total_read = fread(buffer, sizeof(char), LMB_BUFFER_SIZE, fd)) > 0)
    {
        //printf("Total read %d\n", total_read);
        all_read += total_read;
        send(sock, buffer, sizeof(buffer), 0);
        bzero(buffer, sizeof(buffer));
        read(sock, buffer, sizeof(buffer));
    }
    printf("Sended %.0f bytes of data\n", all_read);
    //The file ended so sending the LMB_FILE_ENDED message
    bzero(buffer, sizeof(LMB_BUFFER_SIZE));
    strcpy(buffer, LMB_FILE_ENDED);
    send(sock, buffer, sizeof(buffer), 0);
    // Closing the file
    close(fd);
}

// Recieve the file from a given socket, than creates a new copy
// sock: The socket to recieve the file
// filename: The filename for the file to create
void lmb_recv_file(int sock, char *filename)
{
    char buffer[LMB_BUFFER_SIZE];
    FILE *fd;
    double n_reads = 0;
    double n_read = 0;

    //Opening the file
    fd = fopen(filename, "wb");
    if (fd == NULL)
    {
        printf("Error Opening File\n");
        return;
    }
    //Keeps listening until nothing more comes
    // - Recieve the file from the client (LMB_SEND_FILE_OK if successully or LMB_SEND_FILE_ERR on comunication errors)
    while ((n_read = read(sock, buffer, sizeof(buffer))) > 0)
    {
        //printf("Reading from buffer: %s \n", buffer);
        //Checking if the last buffer was a LMB_FILE_ENDED message
        if (strncmp(buffer, LMB_FILE_ENDED, sizeof(LMB_FILE_ENDED)) == 0)
        {
            printf("[lmb_recv_file] File is ended...\n");
            bzero(buffer, sizeof(buffer));
            strcpy(buffer, LMB_FILE_ENDED_ACK);
            send(sock, buffer, sizeof(buffer), 0);
            break;
        }
        //Else simply send the correct ACK or ERR messages
        else if (n_read > 0)
        {
            //Saving the buffer into the file
            fwrite(buffer, sizeof(char), LMB_BUFFER_SIZE, fd);
            n_reads += n_read;
            bzero(buffer, sizeof(buffer));
            strcpy(buffer, LMB_SEND_FILE_OK);
            send(sock, buffer, strlen(buffer), 0);
        }
        else
        {
            bzero(buffer, sizeof(buffer));
            strcpy(buffer, LMB_SEND_FILE_ERR);
            send(sock, buffer, strlen(buffer), 0);
        }
    }
    printf("Read %.0f bytes of data\n", n_reads);
    //Closing the file
    fclose(fd);
}

// Sends a message to the given socket
// sock: The socket to send the message to
// msg: The message to send
void lmb_send_msg(int sock, char *msg)
{
    char buffer[LMB_BUFFER_SIZE];
    bzero(buffer, LMB_BUFFER_SIZE);
    strcpy(buffer, msg);
    printf("lmb_send_recv_msg will send %s to socket %d\n", msg, sock);
    //sleep(2);
    send(sock, buffer, sizeof(buffer), 0);
}

// Recieves a message from the given socket
// sock: The socket to recieve the message
// buffer: The buffer to store the response from the socket
void lmb_recv_msg(int sock, char *buffer)
{
    bzero(buffer, LMB_BUFFER_SIZE);
    read(sock, buffer, LMB_BUFFER_SIZE);
    printf("lmb_recv_msg recieved message %s from socket %d\n", buffer, sock);
    //sleep(2);
}

//Reads entire file content
char * lmb_load_file(char *file_name)
{
    char *buffer;
    long nbytes;
    FILE *file;

    file = fopen(file_name, "r");
    /* Get the number of bytes */
    fseek(file, 0L, SEEK_END);
    nbytes = ftell(file);
    fseek(file, 0L, SEEK_SET);

    //Allocating memory
    buffer = (char*)calloc(nbytes, sizeof(char));
    if (buffer == NULL) return "\n";
    fread(buffer, sizeof(char), nbytes, file);
    printf(buffer);
    return buffer;
}

// Given a path, it returns it's filename
char *filename_from_path(char *path)
{
    for (size_t i = strlen(path) - 1; i; i--)
    {
        if (path[i] == '/')
        {
            return &path[i + 1];
        }
    }
    return path;
}

//Creates a copy of the given file, after giving the path, to the Lightmap Bakery TMP folder
void lmb_create_tmp_copy(char * path)
{
    FILE *source, *target;
    char ch;
    //Adding ./tmp/ to the filename
    char target_path[256];
    strcpy(target_path, "");
    strcat(target_path, "./tmp/");
    strcat(target_path, filename_from_path(path));

    printf("Source file %s\n", path);
    printf("Target file %s\n", target_path);

    //Opening the source file
    source = fopen(path, "rb");
    if (source == NULL)
    {
        printf("[lmb_create_tmp_copy] Error in opening the source file!!\n");
        return;
    }

    //Opening target file
    target = fopen(target_path, "wb");
    if (target == NULL)
    {
        printf("[lmb_create_tmp_copy] Error in opening the target file!!\n");
        return;
    }

    //Repeat until read at least 1 byte in the buffer
    char buffer[LMB_BUFFER_SIZE];
    while (fread(buffer, sizeof(char), LMB_BUFFER_SIZE, source) > 0)
    {
        fwrite(buffer, sizeof(char), LMB_BUFFER_SIZE, target);
    }
 
    printf("File copied successfully \n");
 
    fclose(source);
    fclose(target);
}

//Removes a file
void lmb_remove_file(char * path)
{
    if (remove(path) != 0)
    {
        printf("Unable to remove the file \n");
    }
}