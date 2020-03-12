if gcc client.c -o ../client `pkg-config --cflags gtk+-3.0` `pkg-config --libs gtk+-3.0`; 
then
    ../client
else
    echo "Errore nel build del client!!"
fi