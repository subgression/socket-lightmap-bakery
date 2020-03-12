// Server side C/C++ program to demonstrate Socket programming
#include <gtk/gtk.h>
#include "../lmb/lightmapbakery.h"

static GtkWidget *window, *left_box, *right_box;
//Log that contains all the messages for the server
static GtkWidget *log = NULL;
enum connection_status actual_status = IDLE;
struct sockaddr_in address; 
int addrlen = sizeof(address); 
char buffer[LMB_BUFFER_SIZE] = {0}; 

int create_socket();
void server_sock_loop(void *);
void init_window();
void add_log_entry(char *);

int main(int argc, char const *argv[])
{
    //pthread that starts the server
    int server_fd = create_socket();
    pthread_t id;
    pthread_create(&id, NULL, server_sock_loop, server_fd);

    //Starting UI
    gtk_init(&argc, &argv);
    init_window();

    return 0;
}

int create_socket()
{
    int server_fd, valread; 
    int opt = 1; 
       
    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 
       
    // Forcefully attaching socket to the port 8080 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( PORT ); 
    // Forcefully attaching socket to the port 8080 
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0) 
    { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 
    if (listen(server_fd, 3) < 0) 
    { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 
    return server_fd;
}

// Server side loop for the socket
void server_sock_loop(void *fd)
{
    int valread;
    int new_socket;
    int server_fd = (int)fd;
    sleep(1);
    printf("Server started...\n");
    add_log_entry("Server started...\n");
    
    while (0 == 0)
    {
        if ((new_socket = accept(server_fd, (struct sockaddr *) &address, (socklen_t*) &addrlen)) != -1) 
        { 
            printf("Client connection to the server!\n");
            int n_read = 0;
            //Reads the header first the choose what to do
            read(new_socket , buffer, sizeof(buffer));
            //Test message sent from client, responding with LMB_TEST_MSG_OK
            if (strncmp(buffer, LMB_TEST_MSG, sizeof(LMB_TEST_MSG)) == 0)
            {
                printf("Requested test message\n");
                add_log_entry("Requested test message\n");
                bzero(buffer, sizeof(buffer));
                strcpy(buffer, LMB_TEST_MSG_OK);
                send(new_socket, buffer, sizeof(buffer), 0);
            }
            //Request from the client to SEND a file to the server if accepted
            // - Recieve the filename
            // - Recieve the paramaters from the client (bake_script, TODO: eventual atlas size and lightmapper size)
            // - Recieve the file from the client (BUFFER_ACK if successully or BUFFER_ERR on comunication errors)
            // - Executes the bake for the given file
            // - Send back a zipped file containing the result of the bake procedure 
            else if (strncmp(buffer, LMB_REQ_SEND_FILE, sizeof(LMB_REQ_SEND_FILE)) == 0)
            {
                printf("Request from the client to bake the file, responding with LMB_ACK_SEND_FILE\n");
                add_log_entry("Request from the client to bake the file\n");
                lmb_send_msg(new_socket, LMB_ACK_SEND_FILE);
                
                // - Recieve the filename
                printf("Waiting for filename...\n");
                add_log_entry("Waiting for filename...\n");
                char filename_to_bake[LMB_BUFFER_SIZE];
                lmb_recv_msg(new_socket, buffer);
                strcpy(filename_to_bake, "");
                strcat(filename_to_bake, "./tmp/");
                strcat(filename_to_bake, buffer);
                //printf("Filename location will be %s\n", filename_to_bake);
                lmb_send_msg(new_socket, LMB_STD_ACK);

                // - Recieve the paramaters from the client (bake_script, TODO: eventual atlas size and lightmapper size)
                printf("Waiting for baking script...\n");
                add_log_entry("Waiting for baking script...\n");
                char bake_script[LMB_BUFFER_SIZE];
                lmb_recv_msg(new_socket, buffer);
                strcpy(bake_script, buffer);
                printf("The bake script is %s\n", bake_script);
                lmb_send_msg(new_socket, LMB_STD_ACK);

                // - Recieve the file from the client (BUFFER_ACK if successully or BUFFER_ERR on comunication errors)
                lmb_recv_file(new_socket, filename_to_bake);

                // - Executes the bake for the given file
                // Composing the schell command to launch
                char cmd[256];
                strcpy(cmd, "");
                strcat(cmd, BLENDER_FILE_PATH);
                strcat(cmd, " --background ");
                strcat(cmd, filename_to_bake);
                strcat(cmd, " --python ./python/");
                strcat(cmd, bake_script);
                //[TODO] Allow the app to accept variable lightmapper samples and atlas size
                // since the other script allows it
                strcat(cmd, " -- 1024 128");
                printf("Executing the script %s \n", cmd);
                add_log_entry("Executing the script:\n");
                add_log_entry(cmd);
                add_log_entry("\n");
                system(cmd);
                //Sending a defualt ACK message to the client
                printf("Sending the default ACK message to the client\n");
                lmb_send_msg(new_socket, LMB_STD_ACK);
                bzero(buffer, LMB_BUFFER_SIZE);

                // - Send back a zipped file containing the result of the bake procedure
                char zipped_prefix[256];
                //strcpy(zipped_prefix, "result_");
                strcpy(zipped_prefix, "");
                strcat(zipped_prefix, filename_to_bake);
                strcat(zipped_prefix, ".zip");
                bzero(buffer, LMB_BUFFER_SIZE);
                printf("Sending the result back to the client %s \n", zipped_prefix);
                add_log_entry("Sending the result back to the cliend \n");
                lmb_send_file(new_socket, zipped_prefix);

                //Removing the result and the file from the tmp folder
                lmb_remove_file(filename_to_bake);
                lmb_remove_file(zipped_prefix);
                //Trying to fetch and delete the savefile for blender (usually foo.blend1)
                char blend1[LMB_BUFFER_SIZE];
                strcpy(blend1, filename_to_bake);
                strcat(blend1, "1");
                lmb_remove_file(blend1);
                add_log_entry("All OK \n");
            }
            //Generic error, no MESSAGE found
            else
            {
                printf("Communication error, no MESSAGE found for type: %s\n", buffer);
                bzero(buffer, sizeof(buffer));
                strcpy(buffer, LMB_NO_PROTO_MSG);
                send(new_socket, buffer, strlen(buffer), 0);
            }
        } 
        printf("Closing connection...\n");
        close(new_socket);
    }
}

/*--------------------------------------
    UI Creation
----------------------------------------*/
void init_window()
{
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Box 
    left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    //"Logo" of the app
    GtkWidget *image;
    image = gtk_image_new_from_file("../lmb/icon.png");
    gtk_box_pack_start(GTK_BOX(left_box), image, TRUE, TRUE, 5);
    //Label for version
    GtkWidget *app_name_label;
    app_name_label = gtk_label_new("Lightmap Bakery Server v0.1a");
    gtk_box_pack_start(GTK_BOX(left_box), app_name_label, TRUE, TRUE, 5);

    //Log box
    right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    //Adding log to the box
    log = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log), FALSE);
    gtk_box_pack_start(GTK_BOX(right_box), log, TRUE, TRUE, 5);

    //Table that handles the two columns
    GtkWidget *table;
    table = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(table), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(table), TRUE);
    gtk_grid_attach(GTK_GRID(table), left_box, 0, 0, 1, 2);
    gtk_grid_attach(GTK_GRID(table), right_box, 1, 0, 1, 2);
    gtk_container_add(GTK_CONTAINER(window), table);

    //Adding css
    GtkCssProvider *cssProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(cssProvider, "../lmb/theme.css", NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(cssProvider),
                                              GTK_STYLE_PROVIDER_PRIORITY_USER);

    gtk_widget_show_all(window);
    gtk_main();
}

//Adds a log entry to the log
void add_log_entry(char * entry)
{
    GtkTextBuffer * text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log));
    gtk_text_buffer_insert_at_cursor(text_buffer, entry, strlen(entry));
    gtk_text_view_set_buffer(log, text_buffer);
}