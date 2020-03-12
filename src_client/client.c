#include <gtk/gtk.h>
#include "../lmb/lightmapbakery.h"

static GtkWidget *window, *left_box, *right_box;
static GtkWidget *file_list_box;
struct bake_file files_list[LMB_MAX_FILES_CLIENT];
int files_list_count = 0;
//Entry for the ip
static GtkWidget *ip_entry;
//Label for connection status
static GtkWidget *connection_status_label;
enum connection_status actual_status = IDLE;
//Is the socket thread already been created?
int socket_created = 0;

/*--------------------------------------
    Prototypes
----------------------------------------*/
void create_client();
void start_client(int);
void print_list_of_files();
void add_button_clicked(GtkWidget *, gpointer);
void connection_test_button_clicked(GtkWidget *, gpointer);
void bake_button_clicked(GtkWidget *, gpointer);
struct bake_file path_to_struct(char *);
char *filename_from_path(char *);
void update_files_ui();
void init_window();

/*--------------------------------------
    Main
----------------------------------------*/
// gcc 007_gtk.c -o 007_gtk `pkg-config --cflags gtk+-3.0` `pkg-config --libs gtk+-3.0`
int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    init_window();
    return 0;
}

/*--------------------------------------
    Networking Functions
----------------------------------------*/
//Creates the socket with the given IP and PORT
int create_socket(char *ip_addr)
{
    int sock;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, ip_addr, &serv_addr.sin_addr) <= 0)
    {
        printf("Invalid address/Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Connection Failed \n");
        return -1;
    }

    return sock;
}

//Sends a test message to the server, awaiting response
void send_test_msg(int sock)
{
    char buffer[LMB_BUFFER_SIZE] = {};
    strcpy(buffer, LMB_TEST_MSG);
    send(sock, buffer, sizeof(buffer), 0);
    bzero(buffer, sizeof(buffer));
    read(sock, buffer, sizeof(buffer));
    printf("Server: %s\n", buffer);
}

//Sends a file to the server, awaiting response
void send_file(int sock, char *filename)
{
    char buffer[LMB_BUFFER_SIZE];
    int total_read = 0;
    long all_read = 0;

    // Trying to send the request to the server and waiting for response
    int send_msg_counter = 0;
    while (send_msg_counter <= 5)
    {
        lmb_send_msg(sock, LMB_REQ_SEND_FILE);
        lmb_recv_msg(sock, buffer);
        send_msg_counter++;
        if (strncmp(buffer, LMB_ACK_SEND_FILE, sizeof(LMB_ACK_SEND_FILE)) == 0)
            break;
    }

    //If we sended too many requests without response, exception error in comunication
    if (send_msg_counter > 5)
    {
        printf("Error! Server not responding... \n");
        return;
    }

    // If the response is a LMB_ACK_SEND_FILE, start sending the file, the steps are:
    // - Send the filename to the server
    // - Send the script to be used for the baking procedure
    // - Send the file to be baked
    // - Recieve the file from the server
    if (strncmp(buffer, LMB_ACK_SEND_FILE, sizeof(LMB_ACK_SEND_FILE)) == 0)
    {
        printf("Server accpted the request, starting to send the file...\n");

        // - Send the filename to the server
        lmb_create_tmp_copy(filename);
        //printf("Sending the filename %s...\n", filename);
        lmb_send_msg(sock, filename);
        lmb_recv_msg(sock, buffer);
        // Handling errors, if the response is not a LMB_STD_ACK there is an error in comunication
        if (strncmp(buffer, LMB_STD_ACK, sizeof(LMB_STD_ACK)) != 0)
        {
            printf("Communication erorr while sending filename!! Message %s\n");
            return;
        }

        // - Send the script to be used for the baking procedure
        //printf("Sending the script...\n");
        lmb_send_msg(sock, "join_n_bake.py");
        lmb_recv_msg(sock, buffer);
        // Handling errors, if the response is not a LMB_STD_ACK there is an error in comunication
        if (strncmp(buffer, LMB_STD_ACK, sizeof(LMB_STD_ACK)) != 0)
        {
            printf("Communication erorr while sending baking script!!\n");
            return;
        }

        // - Send the file to be baked
        //printf("Preparing to send the file %s\n", filename);
        char tmp_filename[LMB_BUFFER_SIZE];
        strcpy(tmp_filename, "./tmp/");
        strcat(tmp_filename, filename);
        sleep(1);
        lmb_send_file(sock, tmp_filename);
        //Removing file from TMP folder
        lmb_remove_file(tmp_filename);
        lmb_recv_msg(sock, buffer);
        bzero(buffer, LMB_BUFFER_SIZE);

        //Waiting for bake process to end
        //printf("Waiting for bake process... (Waiting for ACK)\n");
        //Waiting for the default ACK message
        lmb_recv_msg(sock, buffer);
        // Handling errors, if the response is not a LMB_STD_ACK there is an error in comunication
        if (strncmp(buffer, LMB_STD_ACK, sizeof(LMB_STD_ACK)) != 0)
        {
            printf("Communication erorr while sending baking script!!\n");
            return;
        }
        //printf("Ack recieved! (%s) Downloading result from the server...\n", buffer);

        // - Recieve the file from the server
        bzero(buffer, sizeof(LMB_BUFFER_SIZE));
        char result_filename[LMB_BUFFER_SIZE];
        strcpy(result_filename, "./result/");
        strcat(result_filename, filename);
        strcat(result_filename, ".zip");
        lmb_recv_file(sock, result_filename);
    }
}

// Loop that handles the communication with the given server in the create_function
void client_sock_loop(void *ip_addr)
{
    if (files_list_count <= 0)
    {
        printf("No files to be baked!! Returning...\n");
        return;
    }

    if (strlen((char *)ip_addr) <= 0)
    {
        printf("Must insert a correct IP!! Returning...\n");
        return;
    }

    //Starting the bake procedure for all files currently added
    //Creates the socket for comunication
    for (int i = 0; i < files_list_count; i++)
    {
        int sock = create_socket((char *)ip_addr);
        //Creating a copy in the TMP folder
        //to avoid any path errors
        lmb_create_tmp_copy(files_list[i].path);
        send_file(sock, files_list[i].filename);
        //Closes the socket
        close(sock);
    }
}

/*--------------------------------------
    Debug Functions
----------------------------------------*/
void print_list_of_files()
{
    printf("Lista dei files attualmente in coda \n");
    for (int i = 0; i < files_list_count; i++)
    {
        printf("Nome file: %s \n", files_list[i].filename);
        printf("Script da eseguire: %d \n", files_list[i].script);
    }
}

/*--------------------------------------
    Callbacks per gli elementi dell'UI
----------------------------------------*/
//Quando viene clickato il pulsante, viene visualizzato il dialog e scelto il file da aggiungere alla coda
void add_button_clicked(GtkWidget *add_button, gpointer window)
{
    GtkWidget *dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    gint res;
    dialog = gtk_file_chooser_dialog_new("Open File",
                                         GTK_WINDOW(window),
                                         action,
                                         "Close",
                                         GTK_RESPONSE_CANCEL,
                                         "Open",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT)
    {
        char *path;
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        path = gtk_file_chooser_get_filename(chooser);
        // Adding file to the list
        files_list[files_list_count] = path_to_struct(path);
        files_list_count++;
        // Adding label for the file
        update_files_ui();
        print_list_of_files();
        gtk_widget_destroy(dialog);
    }
}

// After clicking on the bake button
// 1- The system creates the thread (if not started yet)
void bake_button_clicked(GtkWidget *bake_button, gpointer window)
{
    if (socket_created == 0)
    {
        // Obtaining the ip address from the GTK_ENTRY
        char *ip_addr;
        ip_addr = (char *)gtk_entry_get_text(GTK_ENTRY(ip_entry));
        // Creating the thread
        pthread_t id;
        pthread_create(&id, NULL, client_sock_loop, ip_addr);
    }
    //setto lo stato a SEND_FILE
    //actual_status = SEND_FILE;
}

/*--------------------------------------
    Utilities
----------------------------------------*/
// Converts the pathname given by Gtk in a struct needed for Lightmap Bakery
struct bake_file path_to_struct(char *path)
{
    struct bake_file f;
    f.path = path;
    f.filename = filename_from_path(path);
    f.script = GODZARENA_MAP;
    return f;
}

/*--------------------------------------
    Window and UI
----------------------------------------*/
//Updates of the UI creating a subgrid containing all the elements ready to be baked
void update_files_ui()
{
    gtk_widget_destroy(file_list_box);
    file_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    //Adding the header
    GtkWidget *header;
    header = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(header), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(header), 1);

    GtkWidget *file_name_header;
    file_name_header = gtk_label_new("Filename");
    gtk_grid_attach(GTK_GRID(header), file_name_header, 0, 0, 1, 1);

    GtkWidget *script_header;
    script_header = gtk_label_new("Script");
    gtk_grid_attach(GTK_GRID(header), script_header, 1, 0, 1, 1);

    gtk_box_pack_start(GTK_BOX(file_list_box), header, TRUE, TRUE, 0);
    for (int i = 0; i < files_list_count; i++)
    {
        //Adding the box
        GtkWidget *inner_grid;
        inner_grid = gtk_grid_new();
        gtk_grid_set_column_homogeneous(GTK_GRID(inner_grid), TRUE);
        gtk_grid_set_row_spacing(GTK_GRID(inner_grid), 1);
        //label of the file
        GtkWidget *file_label;
        file_label = gtk_label_new(files_list[i].filename);
        gtk_grid_attach(GTK_GRID(inner_grid), file_label, 0, i, 1, 1);
        //Combo box for script selection
        GtkWidget *combo_box;
        combo_box = gtk_combo_box_text_new();
        //Adding the strings to the combo box
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), "1K_join_n_bake.py");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), "2K_join_n_bake.py");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), "1K_bake_all.py");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), "2K_bake_all.py");
        gtk_combo_box_set_active(GTK_COMBO_BOX_TEXT(combo_box), 0);
        gtk_grid_attach(GTK_GRID(inner_grid), combo_box, 1, i, 1, 1);
        //Pack of the inner_box
        gtk_box_pack_start(GTK_BOX(file_list_box), inner_grid, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(right_box), file_list_box, TRUE, TRUE, 10);
    gtk_widget_show_all(window);
}

void init_window()
{
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Box dei bottoni
    left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    //"Logo" of the app
    GtkWidget *image;
    image = gtk_image_new_from_file("../lmb/icon.png");
    gtk_box_pack_start(GTK_BOX(left_box), image, TRUE, TRUE, 5);
    //Label for name and version
    GtkWidget *app_name_label;
    app_name_label = gtk_label_new("Lightmap Bakery Client v0.1a");
    gtk_box_pack_start(GTK_BOX(left_box), app_name_label, TRUE, TRUE, 5);
    //Label for the IP
    GtkWidget *set_ip_label;
    set_ip_label = gtk_label_new("Server IP Address: ");
    gtk_box_pack_start(GTK_BOX(left_box), set_ip_label, TRUE, TRUE, 5);
    //Entry for the IP
    ip_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(left_box), ip_entry, TRUE, TRUE, 5);
    //Button that allows to add a .blend file
    GtkWidget *add_button;
    add_button = gtk_button_new_with_label("Add a file");
    g_signal_connect(add_button, "clicked", G_CALLBACK(add_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(left_box), add_button, TRUE, TRUE, 5);
    //Starts the bake procedure
    GtkWidget *bake_button;
    bake_button = gtk_button_new_with_label("Bake of the files");
    g_signal_connect(bake_button, "clicked", G_CALLBACK(bake_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(left_box), bake_button, TRUE, TRUE, 5);

    //Box for the files
    right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 60);
    //Adding the header
    //gtk_widget_destroy(file_list_box);
    file_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *header;
    header = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(header), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(header), 1);
    GtkWidget *file_name_header;
    file_name_header = gtk_label_new("Filename");
    gtk_grid_attach(GTK_GRID(header), file_name_header, 0, 0, 1, 1);
    GtkWidget *script_header;
    script_header = gtk_label_new("Script");
    gtk_grid_attach(GTK_GRID(header), script_header, 1, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(right_box), file_list_box, TRUE, TRUE, 10);

    //Table that handles the two boxes
    GtkWidget *table;
    table = gtk_grid_new();
    //gtk_grid_set_row_homogeneous(GTK_GRID(table), TRUE);
    //gtk_grid_set_column_homogeneous(GTK_GRID(table), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(table), 20);
    gtk_grid_attach(GTK_GRID(table), left_box, 0, 0, 1, 2);
    gtk_grid_attach(GTK_GRID(table), right_box, 1, 0, 1, 2);
    g_object_set(table, "margin", 20, NULL);
    gtk_container_add(GTK_CONTAINER(window), table);
    gtk_widget_show_all(window);
    gtk_main();
}