# FAT32-File-System-Reader
This program (FAT32.c) takes in the name of a file containing the image of a FAT32 drive. The commands supported are DIR, EXTRACT, and QUIT.
 
To compile the program in Linux use the line:
        "gcc -g FAT32.c -o FAT32" 

To run the program, use the line:
        "./FAT32 Drive.img"
Make sure the file "Drive.img" is a FAT32 formatted image in the same directory as FAT32.c
