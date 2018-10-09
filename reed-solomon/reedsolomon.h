
#include <linux/rslib.h>

static struct rs_control *rs_decoder;

struct encoded_block{

};

void initialize_rs(void);

int encode(uint32_t data_length, uint8_t *data, void *entropy, uint32_t par_length, uint16_t *par);

int decode(uint32_t data_length, uint8_t *data, void *entropy, uint32_t par_length, uint16_t *par);

void cleanup_rs(void);
