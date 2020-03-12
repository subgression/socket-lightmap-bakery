if gcc server.c -o server `pkg-config --cflags gtk+-3.0` `pkg-config --libs gtk+-3.0`; 
then
    ./server
else
    echo "Errore nel build del server!!"
fi