# FAT32-File-System-Reader
This program (FAT32.c) takes in the name of a file containing the image of a FAT32 drive. The commands supported are DIR, EXTRACT, and QUIT.
 
To compile the program in Linux use the line:
        "gcc -g FAT32.c -o FAT32" 

To run the program, use the line:
        "./FAT32 Drive.img"
Make sure the file "Drive.img" is a FAT32 formatted image in the same directory as FAT32.c.

Once the program has started it will prompt the user for a command.

"DIR" will list all the files in the root directory formatted similar to running DIR/X on Windows.

"EXTRACT <filename>" will look for a file named <filename> on the drive and copy it into the same directory as FAT32.c.
 
 "QUIT" will end the program.
