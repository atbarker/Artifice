
#include <linux/rslib.h>
#include <linux/string.h>
#include "reedsolomon.h"

void hexDump (char *desc, void *addr, int len) {
    int i;
    unsigned char buff[17];
    unsigned char *pc = (unsigned char*)addr;

    // Output description if given.
    if (desc != NULL)
        printk (KERN_INFO "%s:\n", desc);

    if (len == 0) {
        printk(KERN_INFO "  ZERO LENGTH\n");
        return;
    }
    if (len < 0) {
        printk(KERN_INFO "  NEGATIVE LENGTH: %i\n",len);
        return;
    }

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                printk (KERN_INFO "  %s\n", buff);

            // Output the offset.
            printk (KERN_INFO "  %04x ", i);
        }

        // Now the hex code for the specific character.
        printk (KERN_INFO " %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printk (KERN_INFO "   ");
        i++;
    }

    // And print the final ASCII bit.
    printk (KERN_INFO "  %s\n", buff);
}

void initialize_rs(){
    rs_decoder = init_rs(10, 0x409, 0, 1, 6);
}

int encode(uint32_t data_length, uint8_t *data, void *entropy, uint32_t par_length, uint16_t *par){
    //uint16_t par[6];
//    memset(par, 0, sizeof(par));
    encode_rs8(rs_decoder, data, 512, par, 0);
    hexDump("data", data, data_length);
    hexDump("parity", par, par_length);    
    return 0;
}

int decode(uint32_t data_length, uint8_t *data, void *entropy, uint32_t par_length, uint16_t *par){
    //uint16_t par[6];
    //uint8_t data[512];
    int numerr;
    numerr = decode_rs8(rs_decoder, data, par, 512, NULL, 0, NULL, 0, NULL);
    hexDump("data", data, data_length);
    return numerr;
}


void cleanup_rs(void){
    free_rs(rs_decoder);
}
