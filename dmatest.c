#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#define S2MM_CR		0x30
#define S2MM_SR		0x34
#define S2MM_DA		0x48
#define S2MM_LENGTH	0x58

#define IOC_IRQ_FLAG	1<<12
#define IDLE_FLAG	1<<1

#define STATUS_HALTED               0x00000001
#define STATUS_IDLE                 0x00000002
#define STATUS_SG_INCLDED           0x00000008
#define STATUS_DMA_INTERNAL_ERR     0x00000010
#define STATUS_DMA_SLAVE_ERR        0x00000020
#define STATUS_DMA_DECODE_ERR       0x00000040
#define STATUS_SG_INTERNAL_ERR      0x00000100
#define STATUS_SG_SLAVE_ERR         0x00000200
#define STATUS_SG_DECODE_ERR        0x00000400
#define STATUS_IOC_IRQ              0x00001000
#define STATUS_DELAY_IRQ            0x00002000
#define STATUS_ERR_IRQ              0x00004000

#define HALT_DMA                    0x00000000
#define RUN_DMA                     0x00000001
#define RESET_DMA                   0x00000004
#define ENABLE_IOC_IRQ              0x00001000
#define ENABLE_DELAY_IRQ            0x00002000
#define ENABLE_ERR_IRQ              0x00004000
#define ENABLE_ALL_IRQ              0x00007000



#define DMA_SIZE			65535
#define BUFF_SIZE			8388607
//OK
unsigned int write_dma(unsigned int *virtual_addr, int offset, unsigned int value)
{
    virtual_addr[offset>>2] = value;

    return 0;
}

//OK
unsigned int read_dma(unsigned int *virtual_addr, int offset)
{
    return virtual_addr[offset>>2];
}

//OK fornow
void dma_s2mm_status(unsigned int *virtual_addr, unsigned int debug)
{
    unsigned int status = read_dma(virtual_addr, S2MM_SR);

    //printf("Stream to memory-mapped status (0x%08x@0x%02x):", status, S2MM_SR);
	if(debug) {
		if (status & STATUS_HALTED) printf(" Halted.\n");
		else printf(" Running.\n");

    	if (status & STATUS_IDLE) printf("Idle.\n");
    	if (status & STATUS_SG_INCLDED) printf("SG is included.\n");
    	if (status & STATUS_DMA_INTERNAL_ERR) printf("DMA internal error.\n");
    	if (status & STATUS_DMA_SLAVE_ERR) printf("DMA slave error.\n");
    	if (status & STATUS_DMA_DECODE_ERR) printf("DMA decode error.\n");
    	if (status & STATUS_SG_INTERNAL_ERR) printf("SG internal error.\n");
    	if (status & STATUS_SG_SLAVE_ERR) printf("SG slave error.\n");
    	if (status & STATUS_SG_DECODE_ERR) printf("SG decode error.\n");
    	if (status & STATUS_IOC_IRQ) printf("IOC interrupt occurred.\n");
    	if (status & STATUS_DELAY_IRQ) printf("Interrupt on delay occurred.\n");
    	if (status & STATUS_ERR_IRQ) printf("Error interrupt occurred.\n");
	}
}

//OK for now
int dma_s2mm_sync(unsigned int *virtual_addr, unsigned int debug)
{
	unsigned int s2mm_status = read_dma(virtual_addr, S2MM_SR);

	// sit in this while loop as long as the status does not read back 0x00001002 (4098)
	// 0x00001002 = IOC interrupt has occured and DMA is idle
	while(!(s2mm_status & IOC_IRQ_FLAG) || !(s2mm_status & IDLE_FLAG))
	{
        dma_s2mm_status(virtual_addr, debug);

	    s2mm_status = read_dma(virtual_addr, S2MM_SR);

		//printf("LENGTH register during write: 0x%08X\n", read_dma(virtual_addr, S2MM_LENGTH));
		
		if(s2mm_status & 0x00000770) {
			//printf("Error ocurred!\n");
			break;
		}
	}
	return 0;
}

unsigned int *virt_addr[2];

void print_mem_dec(void *virtual_address, int byte_count) {
	char *data_ptr = virtual_address;
	int *spi_sample = virtual_address;

	//printf("\nOrdered decimal output for each byte:\n");
	
	int samples = 10;
	int dst_addr = 0x0f000000;

	for(int i = 0; i < samples; i++) {
		for(int j = 0; j < byte_count; j++) {
			printf(" %02d) %04d", i+1, (virt_addr[1])[j]);

			if(i%4 == 3 && i != 0) printf("\n");
			if(i == byte_count/4-1) printf("\nMemory read:\n");
			//dst_addr += 0x40;
		}
	}
}

//Global memory
int ddr_memory;

void initDMA() {

	printf("Initializing DMA...\n");
	ddr_memory = open("/dev/mem", O_RDWR | O_SYNC);
	printf("Opened /dev/mem/\n");
	unsigned int *dma_virtual_addr = mmap(NULL, 65535, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, 0x40400000);
	printf("Got virt addr\n");
	if(dma_virtual_addr == (void *)-1) printf("MMAP error1: %d", errno);
	unsigned int *virtual_dst_addr = mmap(NULL, 8388608, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, 0x0f000000);
	printf("got virt addr 2\n");
	if(virtual_dst_addr == (void *)-1) printf("MMAP error2: %d", errno);


	virt_addr[0] = dma_virtual_addr;
	virt_addr[1] = virtual_dst_addr;

	//close(ddr_memory);
}

void dma_reset() {
	write_dma(virt_addr[0], S2MM_CR, RESET_DMA);
}

void dma_halt() {
	write_dma(virt_addr[0], S2MM_CR, HALT_DMA);
}

void dma_interrupt(unsigned int interrupt) {
		write_dma(virt_addr[0], S2MM_CR, interrupt);
}

void dma_set_da(unsigned int dst_addr) {
	write_dma(virt_addr[0], S2MM_DA, dst_addr);
}

void dma_run() {
	write_dma(virt_addr[0], S2MM_CR, RUN_DMA);
}

void dma_set_length(unsigned int transfer_length) {
	write_dma(virt_addr[0], S2MM_LENGTH, transfer_length);
}

unsigned int dma_get_length() {
	unsigned int length;
	length = read_dma(virt_addr[0], S2MM_LENGTH);

	return length;
}

/* dmaTransfer initiates DMA transfers between SPI Master to memory.
 *
 * One transfer includes 16 SPI samples, sampled at 44.1 kHz
 * SPI sample is 1 word long (4 bytes) with 2 empty bytes
 * 
 * parameters:
 * 		- dst_addr is the physical address to where should DMA send data
 * 		- num_of_transfer is number of DMA transfers to complete
			DMA transfer consists of 16 SPI samples
		- debug is set to 1 if one wants optional printf statements during operation
 */

void dma_transfer(unsigned int dst_addr, unsigned int debug) {
	//Virtual memory of the DMA
	unsigned int *dma_addr = virt_addr[0];

	//Number of SPI samples in a single transfer
	unsigned int num_of_spi_samples = 16;
	//Number of bytes per SPI sample
	unsigned int num_of_bytes = 4;

	//Number of bytes to transfer for a single transfer of 16 SPI samples of 4 bytes
	unsigned int transfer_bytes = num_of_spi_samples * num_of_bytes ; //* num_of_transfers;

	//Static variable that stores if DMA has been reset before transferring data
	static unsigned int has_reset = 0;

	//Reset DMA if first transfer
	if(!has_reset) {
		if(debug) printf("Resetting the DMA\n");
		dma_reset();
		has_reset = 1;
	

	//Halt the DMA
	if(debug) printf("Halting the DMA\n");
	dma_halt();

	//Enable all DMA interrupts
	if(debug) printf("Enabling all interrupts\n");
	dma_interrupt(ENABLE_ALL_IRQ);
	}
	//Setting destination address
	if(debug) printf("Writing destination address\n");
	dma_set_da(dst_addr);



	//Run the DMA
	if(debug) printf("Setting the DMA run bit\n");
	dma_run();
	
	//Write lenght of transfer
	if(debug) printf("Writing length of tranfer -> runs dma transfer\n");
	dma_set_length(transfer_bytes);

	//Sync
	if(debug) printf("Waiting for IOC...\n");
	dma_s2mm_sync(dma_addr, debug);

	//Get transferred lenght in bytes after completion of transfer
	unsigned int length = dma_get_length();
	//if(debug) printf("Transferred %u byte%s", length, (length > 1) ? "s" : "");
	if(debug) printf("%u sample%s", length/4, (length>4) ? "s" : "");
}

void read_dst_addr(unsigned int read_addr, unsigned int num_of_transfers, unsigned int whole_transfer_offset) {
	printf("\n\nReading address space\n\n");
	for(int i = 0; i < num_of_transfers; i++) {
		printf("%08X:\n", read_addr);
		for(int j = 0; j < 16; j++) {
			printf("%02d: %08X ", j+1, *(virt_addr[1] + i * whole_transfer_offset + j));
			if(j%4 == 3) printf("\n");
		}
		read_addr += whole_transfer_offset*4;
	}
}

void read_spi_data(unsigned int read_addr, unsigned int num_of_transfers, unsigned int whole_transfer_offset) {
	printf("\nReading SPI data\n");
	for(int i = 0; i < num_of_transfers; i++) {
		printf("\n%08X: ", read_addr);
		for(int j = 0; j < 16; j++) {
			printf("%4X ", *(virt_addr[1] + i * whole_transfer_offset + j));
			//if(j%4==3) printf("\t");
		}
		read_addr += whole_transfer_offset*4;
	}
	printf("\n");
}

unsigned int write_spi_data(unsigned int num_of_transfers, unsigned int whole_transfer_offset) {

	FILE *fptr = fopen("samples.txt", "w");

	unsigned int *spi_sample = virt_addr[1];
	unsigned int read_addr = 0x0f000000;

	unsigned int whole_length = 0;

	for(int i = 0; i < num_of_transfers; i++) {
		//fprintf("\n%08X: ", read_addr);
		for(int j = 0; j < 16; j++) {
			fprintf(fptr, "%4u ", *(virt_addr[1] + i * whole_transfer_offset + j));
			//if(j%4==3) printf("\t");
			whole_length += dma_get_length();
		}
		read_addr += whole_transfer_offset*4;
		fprintf(fptr, "\n");
	}

	fclose(fptr);

	return whole_length;
	//fprintf(fptr, "\n");
}

int main()
{
	//Initialize the DMA and virtual memory allocation
	initDMA();

	int samples_num = 100;

	int status = 1;

	//Number of bytes to read
	//16 spi_transaction x 4 bytes/spi_transaction = 64
	unsigned int transfer_len = 64;
	//Number of spi_transaction to do
	unsigned int transfer_num = 1;

	unsigned int dst_addr = 0x0f000000;
	unsigned int sample_offset = 0x4;
	unsigned int transfer_offset = 0x40;
	unsigned int whole_transfer_offset = 16;

	unsigned int sample_bytes = transfer_len * transfer_num;

	unsigned int num_of_transfers = 10000;
	unsigned int spi_size = 16;

	//Clear the receive buffer
	memset(virt_addr[1], 0xAB, num_of_transfers * 16 * 4);
	printf("\nCleared dest buffer\n");


	//DMA OPERATIONS

	unsigned int whole_length = 0;
	//Send over DMA
	for(unsigned int i = dst_addr; i < dst_addr + num_of_transfers * 64; i+=64) {

		dma_transfer(i, 0);
		
		/*
		printf("\n%3u: %08X ", i + 1, dst_addr);
		dst_addr += whole_transfer_offset*4;
		unsigned int length = dma_get_length();
		whole_length += length;
		//printf("%02u sample%s", length/4, (length>4) ? "s" : "");
		printf("%3.0lf %% ", (double) (100 * length/4)/spi_size);
		*/
		//usleep(363);
	}

	
	//Reading transferred data after DMA operation
	//Physical address
	unsigned int read_addr = 0x0f000000;
	
	read_spi_data(read_addr, num_of_transfers, whole_transfer_offset);
	
	printf("\n%u / %u samples transferred\n\n", whole_length/4, num_of_transfers * 16);
	
	write_spi_data(num_of_transfers, whole_transfer_offset);

	dma_s2mm_status(virt_addr[0], 1);

	munmap(virt_addr[1], 8388608);
	munmap(virt_addr[0], 65535);
	close(ddr_memory);

	return 0;
}
