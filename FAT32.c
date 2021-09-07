#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
//#include <unistd.h>
#include <locale.h>
#include <wchar.h>

#pragma pack(1)

// Structs
// Every sector is 512 bytes and we will be able to tell the start based on the MBR sector
typedef struct Sector{
    unsigned char sector[512];
} Sector;

// struct for 1 of the 16-byte partition tables in the MBR (ours only has 1 anyway)
typedef struct PartitionTableEntry{
    unsigned char bootFlag;       // Boot Flag
    unsigned char CHSBegin[3];    // used in old CHS partitions
    unsigned char typeCode;       // IMPORTANT partition type code (0x0c for FAT32 with LBA)
    unsigned char CHSEnd[3];      // used in old CHS partitions
    unsigned char LBABegin[4];    /* IMPORTANT the logical block address(LBA) of the start of FAT 
						             (will not be 0, we will use this a lot)
						             Make a global variable and set it?	 */
    unsigned char NumSectors[4];      
} PartitionTableEntry;

// struct for the first sector on the drive - Master Boot Record
typedef union {
    struct {
    	unsigned char       bootCode[446]; // first 446 bytes are boot code
    	PartitionTableEntry part1;         // followed by 64-bytes for the partition table 
    	PartitionTableEntry part2;         
    	PartitionTableEntry part3;
    	PartitionTableEntry part4;
    	unsigned short      flag;          // the last 2 bytes of the MBR will alwaays be 0x55AA
    };
    unsigned char sector[512];
} MBR;                                 

// Bios-Parameter Block struct 
typedef union {
    struct {
    	unsigned char  BS_jmpBoot[3];    // Jump instruction to boot code
    	unsigned char  BS_OEMName[8];    // 8-Character string (not null terminated)
    	unsigned short BPB_BytsPerSec;   // Better be 512
    	unsigned char  BPB_SecPerClus;   // How many sectors make up a cluster
    	unsigned short BPB_RsvdSecCnt;   // # of reserved secs at the beginning (including the BPB)?
    	unsigned char  BPB_NumFATs;      // How many copies of the FAT are there? (had better be 2)
    	unsigned short BPB_RootEntCnt;   // ZERO for FAT32
    	unsigned short BPB_TotSec16;     // Zero for FAT32
    	unsigned char  BPB_Media; 	     // SHOULD be 0xF8 for "fixed", but isnt critical for us
    	unsigned short BPB_FATSz16;      // ZERO for FAT32
    	unsigned short BPB_SecPerTrk;    // Don't care; we're using LBA; no CHS
    	unsigned short BPB_NumHeads;     // Don't care ""
    	unsigned int   BPB_HiddSec;      // Don't care?
    	unsigned int   BPB_TotSec32;     // Total number of sectors on the volume 
    	unsigned int   BPB_FATSz32;      // How many sectors long is ONE copy of the FAT
    	unsigned short BPB_Flags; 	     // Flags; see document
    	unsigned short BPB_FSVer;        // Version of the file system
    	unsigned int   BPB_RootClus;     // Cluster where the root directory starts (should be 2)
    	unsigned short BPB_FSInfo;       // What sector is the FSINGO struct located in? Usually 1
    	unsigned short BPB_BkBootSec;    // REALLY should be 6 - (sec # of the boot record backup)
    	unsigned char  BPB_Reserved[12]; // Should be all zeroes -- reserved for future use
    	unsigned char  BS_DrvNum; 	     // Drive number for int 13 access (ignore)
    	unsigned char  BS_Reserved1;     // Reserved (should be 0)
    	unsigned char  BS_BootSig; 	     // Boot Signature (must be 0x29)
    	unsigned char  BS_VolID[4];      // Volume ID
    	unsigned char  BS_VolLab[11];    // Volume Label
    	unsigned char  BS_FilSysType[8]; // Must be "FAT32    "
    	unsigned char  unused[420];      // not used
    	unsigned char  signature[2];     //MUST BE 0x55 AA
    };
    unsigned char sector[512];
} BPB;

// Directory entry struct
typedef union {    
    struct {
	unsigned char  DIR_Name[11];      // filename and extension. Padded with spaces  --  See MSFT FAT32 doc
	unsigned char  DIR_Attr;          // file attributes
	unsigned char  DIR_NTRes;         // always 0
	unsigned char  DIR_CrtTimeTenth;  // file creation time, fractional portion MILLISECONDS
	unsigned short DIR_CrtTime;       // file creation TIME
	unsigned short DIR_CrtDate;       // file creation DATE 
	unsigned short DIR_LstAccDate;    // Last access time (read or write)
	unsigned short DIR_FstClusHI;     // HIGH WORD of this entry's first cluster number
	unsigned short DIR_WrtTime;	      // time of last write
	unsigned short DIR_WrtDate;       // date of last write
	unsigned short DIR_FstClusLO;     // LOW WORD of this entry's cluster number
	unsigned long  DIR_FileSize;	  // 32-bit DWORD holding this file's size in BYTES
    };
    unsigned char directoryEntry[32];
    struct {
	//struct for Long File Name (LFN) support here
	// union of 2 structs for file entry support
	unsigned char  LDIR_Ord;          // The order of this entry of long dir entries 
	unsigned short LDIR_Name1[5];     // Chars 1-5 of the long-name sub-component in this entry
	unsigned char  LDIR_Attr;         // Attribute must be 0x0F
	unsigned char  LDIR_Type;	      // if 0 indicates an entry that is a sub-component of LFN
	unsigned char  LDIR_Chksum;	      // checksum of name in the short dir entry
	unsigned short LDIR_Name2[6];     // Chars 6-11 of the long-name sub-comp in this dir entry
	unsigned short LDIR_FstClusLO;    // MUST be ZERO
	unsigned short LDIR_Name3[2];     // Chars 12-13 
    };
} DIR;

// Variables
FILE          *fileptr;      // The image we will read in 
unsigned int  fatLBA;        // Start of the FAT32 File System, represents the offset we need to use

// BPB info 
unsigned long sectorsPerCluster;
unsigned long bytesPerSector;
unsigned long reservedSectors;
unsigned long sectorsPerFAT;
unsigned long dirEntriesPerSector; 

unsigned long dataSectorStart; // start of cluster 2, the first sector of the root directory

int*          FATEntries;
unsigned char *buffer;
unsigned char sectorBuffer[512];
unsigned char entryBuffer[4];

MBR mbr;
BPB bpb;
DIR dirEntry;

int numFiles;
unsigned long totalFileSize;

// Functions
void printBytes(void* arr, int n);
void displaySector(unsigned char* sector);

void          readEntry(unsigned long sectorNum, unsigned long entryNum, unsigned char *buffer);
void          readRootDir(unsigned long cluster);
unsigned long readFAT(unsigned long cluster);

unsigned long getNextCluster(DIR dirEntry);
unsigned long getFirstSector(unsigned long cluster);

void          removeSpaces(unsigned char old[], unsigned char new[]);
void          addDot(unsigned char file[]);
unsigned char ChkSum(unsigned char *pFcbName);

void          copyFile(unsigned long cluster, FILE *fp);
void          readFile(unsigned long cluster, char *fileName);

int main( int argc, char *argv[] ){
    // allocate 32 bytes to buffer, used for reading 32 byte entries
    buffer = malloc(32);

    // if statements to confirm proper command line arguments
    if( argc == 2 ){
		// Here we will read the image of a FAT32 drive
		// USE UNSIGNED FOR MOST DATATYPES 
		printf("The drive image supplied is %s\n", argv[1]);

		fileptr = fopen(argv[1], "rb"); // open the file in read binary mode
		fseek(fileptr, 0, SEEK_SET);    // make sure we are at the start of the file

		// read 512 bytes, 1 at a time, FROM fileptr current position INTO sectorBuffer
		fread(sectorBuffer, 512, 1, fileptr);

		// get master boot record sector and display it
		mbr = *(MBR*)sectorBuffer;

		// displaySector((unsigned char*) &mbr);
		/*
		printf("First Sector of FAT Partition: %02X %02X %02X %02X\n",										 mbr.part1.LBABegin[0], 									     mbr.part1.LBABegin[1],										 mbr.part1.LBABegin[2],
					     mbr.part1.LBABegin[3]);
 
		printf("FAT Sector Number: %d\n", mbr.part1.LBABegin[0]);
		*/

		// Assign start of FAT LBA to global variable
		fatLBA = (unsigned int)mbr.part1.LBABegin[0]; 

		fseek(fileptr, 0, SEEK_SET);            // Reset the cursor to start of image
		fseek(fileptr, fatLBA * 512, SEEK_SET); // Set cursor to start of FAT32 partition
		fread(sectorBuffer, 512, 1, fileptr);   // Read first sector of FAT32 - BPB

		bpb = *(BPB*)sectorBuffer;	        // BIOS-Parameter block struct creation

		// printf("Size of BPB is %i\n", sizeof(bpb));
		// displaySector((unsigned char*) &bpb);

	// Get BPB info
		sectorsPerFAT = bpb.BPB_FATSz32;
		// printf("Number of sectors in ONE copy of the FAT: %lu\n", sectorsPerFAT);

		sectorsPerCluster = bpb.BPB_SecPerClus;
		// printf("Sectors Per Cluster: %lu\n", sectorsPerCluster);
		// printf("Clusters in one copy of the FAT: %lu\n", sectorsPerFAT / sectorsPerCluster);

		reservedSectors = bpb.BPB_RsvdSecCnt;
		// printf("Start of FAT is at sector: %lu\n", fatLBA + reservedSectors);

		dataSectorStart = bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * bpb.BPB_FATSz32);

	// Go to the start of the FAT and display the first sector
		fseek(fileptr, 0, SEEK_SET);
		fseek(fileptr, (fatLBA + reservedSectors) * 512, SEEK_SET);
		fread(sectorBuffer, 512, 1, fileptr);

		// printf("First Sector of the FAT\n");
		// displaySector(sectorBuffer);

	// READ IN FAT -- first sector
		FATEntries = (int*)sectorBuffer;

	/*  Below is code that was used for testing purposes only	
		// READ in the first 32 bytes of the root directory to show next root directory cluster	
		fseek(fileptr, 0, SEEK_SET);
		fseek(fileptr, ((fatLBA + reservedSectors) * 512) + bpb.BPB_RootClus * 4, SEEK_SET);	
		fread(buffer, 32, 1, fileptr);

		// printf("First Cluster of the root directory (cluster 2): %08X\n", bpb.BPB_RootClus);
		// printf("Clusters dont start until the FAT\n");
		// printf("\nWe know that the next CLUSTER of the root directory is at FAT[2]: ");
		// printBytes(buffer, 4 * sizeof(unsigned char));

		// printf("FAT [2]: %d\n", FATEntries[2]);	
		// FAT[5669] is in sector (5669 / 128) = 44

		fseek(fileptr, 0, SEEK_SET);
		fseek(fileptr, ((fatLBA + reservedSectors + (FATEntries[bpb.BPB_RootClus] / 128)) * 512),
		         		SEEK_SET);	
		fread(sectorBuffer, 512, 1, fileptr);
		displaySector(sectorBuffer);
		FATEntries = (int*)sectorBuffer;

		printf("FAT [5669]: %08X\n", FATEntries[5669 % 128]);
		printf("\n");

		// The location of the first cluster of the root directory from fat32 documentation
		dataSectorStart = bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * bpb.BPB_FATSz32);

		// output a directory entry not a sector (32 bytes vs 512)	
		// printf("The first 32 byte entry in the root directory:\n");
	
		fseek(fileptr, 0, SEEK_SET);
		fseek(fileptr, (dataSectorStart + fatLBA) * 512, SEEK_SET);
		fread(buffer, 32, 1, fileptr);
		// printBytes(buffer, 32 * sizeof(unsigned char));

		fseek(fileptr, 0, SEEK_SET);
		fseek(fileptr, (dataSectorStart + fatLBA) * 512, SEEK_SET);
		fread(sectorBuffer, 512, 1, fileptr);
		displaySector(sectorBuffer);
		printf("This is the data in the first sector of cluster 2 (the root directory)\n");
	*/

		// Below are the variable declarations to deal with the commands we support
		bool quit   = false;
		char cmd1[] = "DIR";
		char cmd2[] = "EXTRACT";
		char cmd3[] = "QUIT";
		char command[256] = {'\0'};

		while(!quit){
	    	printf("\nPlease enter a command:\n");
		    printf(">");
		    // get input here
		    fgets(command, 256, stdin); // puts(command); - will store fgets somewhere

	    	// here we will support the DIR command, will list all files in the current(root) dir
	    	if(strncmp(command, cmd1, 3) == 0){
				// check for files? -- need to write a function to check
				bool isFiles = true;
				// sector after FAT (both copies) -- where the data sector starts
				dataSectorStart = bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * bpb.BPB_FATSz32);

				if(isFiles){
		    		printf("Files:\n");	

		    		// read the directory for files
		    		readRootDir(bpb.BPB_RootClus);
		    		// Print a summary of the files from the root directory
    		   		printf("\nSummary: Number of Files: %d Size of Files: %'ld\n", 
						     numFiles, totalFileSize);
				}
				else{
		    		printf("File not found\n");
				}
	    	}
	    	// here we support the EXTRACT command
			// EXTRACT will be followed by a filename
	    	else if(strncmp(command, cmd2, 7) == 0){
				bool          fileCopied = false;
				unsigned long cluster;
				DIR           fileDIREntry;
		
				// break the command into tokens, on a space and then on "\0"
	        	char *fileToRead = strtok(command, " ");
	        	fileToRead = strtok(NULL, "\0");	 // "\0" --works with 8.3   
	
				// get the filename, find that file if it exists 
				readFile(bpb.BPB_RootClus, fileToRead);	
		
				fileDIREntry = *(DIR*)buffer;
    
	        	printf("\n File to read: %s", fileToRead);

				printf("  Size of File to be copied: %ld", fileDIREntry.DIR_FileSize);

				// Consult the FAT at cluster to see if data continues
				// printf("\nData starts in cluster: %d\n", fileDIREntry.DIR_FstClusLO);
				cluster = fileDIREntry.DIR_FstClusLO;		

				char trimmedFileName[(strlen(fileToRead) - 4)];

				// Set trimmedFileName to fileToRead minus last char 
				int index;
	    		for(index = 0; index < strlen(fileToRead); index++){
		    		int c = (unsigned int)fileToRead[index];
	            	if (c >= 32 && c <= 127){
		    			trimmedFileName[index] = fileToRead[index];		
	           	 	}
				}	
				// printf("Trimmed file Name: %s\n", trimmedFileName);
	 		
				// Create a new file to extract into
 				FILE *fp;
				fp = fopen(trimmedFileName, "w+b");

				// Copy the contents of the file 
				copyFile(cluster, fp);
				// close the file
				fclose(fp);
	    	}
	    	// here we support the QUIT command 
	    	else if(strncmp(command, cmd3, 4) == 0){
				quit = true;
	   		}
	    	// default if command is invalid
	    	else{
				printf("Invalid command entered, please try again.\n");
				printf("The valid commands are:\nDIR\nEXTRACT <filename>\nQUIT\n");
	    	}
		}
    }
    else if( argc > 2 ){
		printf("Too many arguments supplied.\n");
    }
    else{
		printf("One argument expected.\n");
    }

    fclose(fileptr); 				 // Close the file

    return 0;
}

// function to print the elements of an array in hexadecimal
// takes a void pointer to an array of anything and the number of elements(with size of datatype
	// below is the syntax for using the printBytes function
	// this example prints 4 bytes from an array of char
	// call function -- printBytes(command, 4 * sizeof(char));
	// in function   -- printf("%02X ", ((char*)arr)[i]);
void printBytes(void* arr, int n){
    int i;
    for(i = 0; i < n; i++){
	// See above for example of calling this 
	// must cast to the correct type
	printf("%02X ", ((unsigned char*)arr)[i]);
    }
    printf("\n");
}

void displaySector(unsigned char* sector){
    // display the contents of sector[] as 16 rows of 32 bytes each
    // each row is shown as 16 byte, a "-", and then 16 more bytes
    // the left part of the display is in hes; the right part is in ASCII
    // (if the character is pritnable; otherwise we display "."
    
    for(int i = 0; i < 16; i++){
		for(int j = 0; j < 32; j++){
	    	printf("%02X ", sector[i * 32 + j]);
	    	if(j % 32 == 15){
			printf("- ");
	    	}
		}	
		printf(" ");
	/*
		for(int j = 0; j < 32; j++){
	    	if (j == 16) printf(" ");
	    		int c = (unsigned int)sector[i * 32 + j];
	    	if (c >= 32 && c <= 127) printf("%1c", c);
	    	else                     printf(".");
		}
	*/
		printf("\n");
    }
}

void readEntry(unsigned long sectorNum, unsigned long entryNum, unsigned char *buffer){
    int i, j, k;
    // set the cursor to the beginning of the entry
    fseek(fileptr, 0, SEEK_SET);
    // set the cursor to the start of the ENTRY
    fseek(fileptr, (fatLBA + sectorNum) * 512 + (entryNum * 32), SEEK_SET);
    // read the 32-byte entry into buffer
    fread(buffer, 32, 1, fileptr);
}

// output files here - must be in the format:
// Date & Time of creation Size          Filename(8.3 and LFN formats)
// 04/20/2021  01:09 PM    1,024,450,560 CFIMAG~1.IMG CFImage32.img
void printFileEntry(){
    dirEntry = *(DIR*)buffer;

    // example to return an int from bits
    // int day   = (((1 << j) - 1) & (dirEntry.DIR_CrtDate >> (i)));
    // where i is the start position and j is the number of bits to read
    int day   = (((1 << 5) - 1) & (dirEntry.DIR_CrtDate >> (0)));
    int month = (((1 << 4) - 1) & (dirEntry.DIR_CrtDate >> (5)));
    int year  = 1980 + (((1 << 7) - 1) & (dirEntry.DIR_CrtDate >> (9)));

    printf("\n%02d/%2d/%d ", month, day, year);

    int hour   = (((1 << 5) - 1) & (dirEntry.DIR_CrtTime >> 11));
    int minute = (((1 << 6) - 1) & (dirEntry.DIR_CrtTime >> 5));
    char period[2] = "AM";
    if(hour > 12){
		period[0] = 'P';
		period[1] = 'M';
		hour   = hour - 12;
    }else if(hour = 0){
		hour   = 12;
    }else if(hour = 12){
		period[0] = 'P';
		period[1] = 'M';
    }

    printf("%2d:%2d %s", hour, minute, period);

    int fileSize = (unsigned int)dirEntry.DIR_FileSize;
    totalFileSize = totalFileSize + fileSize;
    setlocale(LC_ALL, "");
    printf(" %'10d ", fileSize);

    // printf("\nEntry Attr: %02X ", thisDirEntry.DIR_Attr);	
}

void copyFile(unsigned long cluster, FILE *fp){
    bool fileCopied = false;
    int  i, j, k;

    unsigned long nextCluster;
    unsigned long firstSec = getFirstSector(cluster);


    for(i = 0; i < sectorsPerCluster && !fileCopied; i++){

		// Read in sector i of the cluster 
		fseek(fileptr, 0, SEEK_SET);
		fseek(fileptr, ((firstSec + i + fatLBA) * 512) , SEEK_SET);
		fread(sectorBuffer, 512, 1, fileptr);
		// displaySector(sectorBuffer);

		// copy sectorBuffer to new file -- each sector is 512 bytes
		for(j = 0; j < 512; j++){
	   		// if(sectorBuffer[j] != 0x00){
			// copy char to new file 
			fputc(sectorBuffer[j], fp);
	   		// }

		}
    }
    nextCluster = readFAT(cluster);
    //displaySector(sectorBuffer);
 
    if(nextCluster < 0x0FFFFFF7){
		copyFile(nextCluster, fp);
    }
	else{
		fileCopied = true;
    }

}

void readFile(unsigned long cluster, char *fileToRead){
    bool          fileRead = false;
    int           i, j, k;               // indexing/loop variables
    unsigned char fileName[12];          // the name of the file as it is stored
    unsigned char fileNameNoPadding[12]; // the name of the file we will format and work with
    DIR           thisDirEntry;          // current 32-byte entry
    DIR           newFile;
    bool          LFN = false;           // 8.3 or LFN?
    unsigned long nextCluster;           // the next cluster 
    unsigned char checkSum;              // checkSum for LFN, from FAT32 doc

    // Get the first sector of the cluster
    unsigned long firstSec = getFirstSector(cluster);

    // Read each sector in the cluster
    for(i = 0; i < sectorsPerCluster && !fileRead; i++){
    // for(i = 0; i < 1; i++){ // read just the first sector for testing 
		// Read each directory entry in sector 
		for(j = 0; j < 512 / 32 && !fileRead; j++){
	    	// send ENTRY location (32 bytes) and store it in buffer
	    	readEntry(firstSec + i, j, buffer);

	    	// cast buffer to DIR struct
	    	newFile      = *(DIR*)buffer;
	    	thisDirEntry = *(DIR*)buffer;

	    	// if(thisDirEntry.DIR_Attr == 0x20){ 
	        	// printf("\nEntry Attr: %02X ", thisDirEntry.DIR_Attr);
			// read in the file name from struct to char array
	    	for(k = 0; k < 11; k++){
		    	// fileName[k] = (unsigned char)thisDirEntry.DIR_Name[k];
		    	fileName[k] = thisDirEntry.directoryEntry[k];
	    	}
			checkSum = ChkSum(fileName);
	    	// null terminate the end of the filename 
	    	fileName[11] = '\0';
	    	// Add dot between file name and extension
	    	addDot(fileName);
	    	// Remove padding/spaces from file name
	    	removeSpaces(fileName, fileNameNoPadding);	

			// printf("FileNameNoPadding: %s", fileNameNoPadding);

			// Return file found success with DIREntry stored in buffer
			if(strncmp(fileNameNoPadding, fileToRead, 12) == 0){
		    	fileRead = true;
		    	// printf("\nFile To Read: %s", fileToRead);
		    	// printf("\nFileNameNoPadding: %s", fileNameNoPadding);
			}

	    	// Print the file name in ASCII representation 
	    	for(k = 0; k < strlen(fileNameNoPadding); k++){
		    	int c = (unsigned int)fileNameNoPadding[k];
	            if (c >= 32 && c <= 127){
				// IF ~ THEN LFN STUFF
	            }
		    	if(c == 126){
					//fileRead = false;
					LFN = true;
					//checkSum = ChkSum(fileName);	       	    	
		    	}
	    	}

			// If LFN is true we need to print the long filename as well
			if(LFN){
		    	int counter = 1;

		    	unsigned char tempCheckSum;
		    	unsigned char longFileName[255] = {'\0'};
		    	int           longFileNameIndex = 0;
		    	do{
					// TODO - write entry to global variable?

					// Read the previous entry
					readEntry(firstSec + i, j - counter, buffer);		    

	    			// cast buffer to DIR struct
	    			thisDirEntry = *(DIR*)buffer;
			
					int nameIndex = 0;
					// printf("\nName1:");
					for(nameIndex = 0; nameIndex < 5; nameIndex++){
			    		// printf(" %04X", thisDirEntry.LDIR_Name1[nameIndex]);		
			    		longFileName[longFileNameIndex] = thisDirEntry.LDIR_Name1[nameIndex];
			    		longFileNameIndex++;
					}
					// printf("\nName2:");
					for(nameIndex = 0; nameIndex < 6; nameIndex++){
			    		// printf(" %04X", thisDirEntry.LDIR_Name2[nameIndex]);		
			    		longFileName[longFileNameIndex] = thisDirEntry.LDIR_Name2[nameIndex];
			    		longFileNameIndex++;
					}
					// printf("\nName3:");
					for(nameIndex = 0; nameIndex < 2; nameIndex++){
			    		// printf(" %04X", thisDirEntry.LDIR_Name3[nameIndex]);		
			    		longFileName[longFileNameIndex] = thisDirEntry.LDIR_Name3[nameIndex];
			    		longFileNameIndex++;
					}

					// increment counter to read the previosu entry
					counter++;
    		    }while(checkSum == thisDirEntry.LDIR_Chksum);
/*
			printf("\nLong File Name: %s", longFileName);
			printf(" LFN strlen: %d", strlen(longFileName));
			printf("\nFile to Read: %s", fileToRead);
			printf(" FtR strlen: %d", strlen(fileToRead));

			int checkName = strncmp(longFileName, fileToRead, strlen(longFileName));
			printf("  strcmp result: %d", checkName);
*/
		    	if(strncmp(longFileName, fileToRead, strlen(longFileName)) == 0){
		    		readEntry(firstSec + i, j, buffer);
		    		thisDirEntry = *(DIR*)buffer;
					// printf("\nLong File Name: %s", longFileName);
					// printf("Testing to see if we get to this point");
		    		fileRead = true;
		    	}

			}
			LFN = false;
	    // }
		}
    }
    // check for next cluster of directory
    nextCluster = readFAT(cluster);
    if(nextCluster < 0x0FFFFFF7 && !fileRead){
		readFile(nextCluster, fileToRead);
    }else{
	//	return newFile;
    }
}

// Read through all the files in the root directory
// Send the FIRST cluster of the root directory 
void readRootDir(unsigned long cluster){
    int           i, j, k;               // indexing/loop variables
    unsigned char fileName[12];          // the name of the file as it is stored
    unsigned char fileNameNoPadding[12]; // the name of the file we will format and work with
    DIR           thisDirEntry;          // current 32-byte entry
    bool          LFN = false;           // 8.3 or LFN?
    unsigned long nextCluster;           // the next cluster 
    unsigned char checkSum;              // checkSum for LFN, from FAT32 doc

    // Get the first sector of the cluster
    unsigned long firstSec = getFirstSector(cluster);

    // Read each sector in the cluster
    for(i = 0; i < sectorsPerCluster; i++){
    // for(i = 0; i < 1; i++){ // read just the first sector for testing 
		// Read each directory entry in sector 
		for(j = 0; j < 512 / 32; j++){
		    // send ENTRY location (32 bytes) and store it in buffer
		    readEntry(firstSec + i, j, buffer);

		    // cast buffer to DIR struct
	    	thisDirEntry = *(DIR*)buffer;
	    	//    printf("\nEntry Attr: %02X ", thisDirEntry.DIR_Attr);
	    	// IF LFN format?
	    	if(thisDirEntry.DIR_Attr == 0x0F){
	        	// printf("\nEntry Attr: %02X ", thisDirEntry.DIR_Attr);
	    	}else if(thisDirEntry.DIR_Name[0] == 0xE5 || thisDirEntry.DIR_Name[0] == 0x00){
				// this entry is unused
	    	}else if(thisDirEntry.DIR_Attr == 0x08){
	        	// This is the name of the volume
				printf("Volume Label: ");
				fileName[11] = '\0'; 
	    		for(k = 0; k < strlen(fileName); k++){
		    		int c = (unsigned int)fileName[k];
	            	if (c >= 32 && c <= 127){
		    			printf("%1c", c);
	            	}
	    		}
	    	}else if(thisDirEntry.DIR_Attr == 0x20){ 
				numFiles++;
	
				printFileEntry();
 	  			// 8.3 FileName 
	        	// printf("\nEntry Attr: %02X ", thisDirEntry.DIR_Attr);
				// read in the file name from struct to char array
	    		for(k = 0; k < 11; k++){
		    		// fileName[k] = (unsigned char)thisDirEntry.DIR_Name[k];
		    		fileName[k] = thisDirEntry.directoryEntry[k];
	    		}
				checkSum = ChkSum(fileName);
	    		// null terminate the end of the filename 
	    		fileName[11] = '\0';
	    		// Add dot between file name and extension
	    		addDot(fileName);
	    		// Remove padding/spaces from file name
	    		removeSpaces(fileName, fileNameNoPadding);	

	    		//printf("\nFile #%d : ", numFiles);

	    		// Print the file name in ASCII representation 
	    		for(k = 0; k < strlen(fileNameNoPadding); k++){
		    		int c = (unsigned int)fileNameNoPadding[k];
	           		if (c >= 32 && c <= 127){
						// IF ~ THEN LFN STUFF
		    			printf("%1c", c);
	            	}
		    		if(c == 126){
						LFN = true;
						//checkSum = ChkSum(fileName);	       	    	
		    		}
	    		}
				// If LFN is true we need to print the long filename as well
				if(LFN){
		    		int counter = 1;
					printf(" ");	

		    		unsigned char tempCheckSum;
		   	 		unsigned char longFileName[255];
	 	    		int           longFileNameIndex = 0;
		    		do{
						// Read the previous entry
						readEntry(firstSec + i, j - counter, buffer);		    

	    				// cast buffer to DIR struct
	    				thisDirEntry = *(DIR*)buffer;
			
						int nameIndex = 0;
						// printf("\nName1:");
						for(nameIndex = 0; nameIndex < 5; nameIndex++){
			    			// printf(" %04X", thisDirEntry.LDIR_Name1[nameIndex]);		
			    			longFileName[longFileNameIndex] = thisDirEntry.LDIR_Name1[nameIndex];
			    			longFileNameIndex++;
						}
						// printf("\nName2:");
						for(nameIndex = 0; nameIndex < 6; nameIndex++){
			    			// printf(" %04X", thisDirEntry.LDIR_Name2[nameIndex]);		
			    			longFileName[longFileNameIndex] = thisDirEntry.LDIR_Name2[nameIndex];
			    			longFileNameIndex++;
						}
						// printf("\nName3:");
						for(nameIndex = 0; nameIndex < 2; nameIndex++){
			    			// printf(" %04X", thisDirEntry.LDIR_Name3[nameIndex]);		
			    			longFileName[longFileNameIndex] = thisDirEntry.LDIR_Name3[nameIndex];
			    			longFileNameIndex++;
						}

						// increment counter to read the previosu entry
						counter++;
    				}while(checkSum == thisDirEntry.LDIR_Chksum);
		    		int prtCtr;
		    		for(prtCtr = 0; prtCtr < strlen(longFileName); prtCtr++){
						if(longFileName[prtCtr] != 0x0000){
			    			printf("%1c", (wchar_t)longFileName[prtCtr]);
		    			}
		    		} 
				}
				LFN = false;
	    	}
		}
    }
    // check for next cluster of directory
    nextCluster = readFAT(cluster);
    // printf("\nNext Cluster Number: %lu\n", nextCluster);
    // IF yes go through this function again
    // IF no the directory ends in the current cluster
    if(nextCluster < 0x0FFFFFF7){
		readRootDir(nextCluster);
    }
}

// from FAT32 doc
unsigned char ChkSum(unsigned char *pFcbName){
    short FcbNameLen;
    unsigned char Sum;

    Sum = 0;
    for(FcbNameLen=11; FcbNameLen!=0; FcbNameLen--){
		// NOTE: the operation is an unsigned char rotate right
		Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return (Sum);
}


// Get the sector number of the first sector of a cluster
unsigned long getFirstSector(unsigned long cluster){
    return dataSectorStart + (cluster - 2) * sectorsPerCluster;
}

unsigned long getNextCluster(DIR dirEntry){
    return ((dirEntry.DIR_FstClusHI << 16) + dirEntry.DIR_FstClusLO);
}

// function to add a dot to a file between the file name and extension
void addDot(unsigned char file[]){
    int  i;
    unsigned char temp = '.';
    unsigned char temp2;    
    
    // set i to be 3 characters from the end of the file (extension is 3 char)
    for(i = strlen(file) - 3; i < strlen(file) + 1; i++){
		temp2   = file[i]; // set temp2 to the char at i
		file[i] = temp;    // set the char at i to temp (first time through is a .)
		temp    = temp2;   // set temp to the char ORIGINALLY at i so we can use it again next loop
    } 
}

// function to remove the padding (spaces) from a file name and extension
void removeSpaces(unsigned char old[], unsigned char new[]){
    int i, j = 0;
    // for each character in the filename
    for(i = 0; i < strlen(old); i++){
		// if the character is not a space 
		if(old[i] != ' '){
	    	// write the character to the new array
	    	new[j] = old[i];
	    	// move to the next index of the new array
	    	j++;
		}
    }
    // null terminate the end to deal with different file lengths?
    new[j] = '\0';
}


unsigned long readFAT(unsigned long cluster){
    unsigned long nextCluster;
    // goto start of the fat and then the sector this cluster is in
    fseek(fileptr, ((fatLBA + reservedSectors + ((int)cluster / 128)) * 512), SEEK_SET);	
    fread(sectorBuffer, 512, 1, fileptr);
    // read in the whole sector as ints (128 FAT entries)
    FATEntries = (int*)sectorBuffer;
    // if cluster / 128 gives us the sector ; cluster % 128 gives us the entry
    nextCluster = FATEntries[cluster % 128];
    //printf("\nNext Cluster Number: %d %08X\n", nextCluster, nextCluster);
    return nextCluster;
}


